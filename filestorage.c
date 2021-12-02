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

#ifndef DEBUG
#define DEBUG 1
#endif

#define MAX_BUCKET 100
#define WRITE_LOG(...){ \
    if ( pthread_mutex_lock(&lock_log) == -1 ){ perror("pthread_mutex_lock"); exit(-1); } \
    /* printf(__VA_ARGS__); */\
    if ( pthread_mutex_unlock(&lock_log) == -1 ){ perror("pthread_mutex_unlock"); exit(-1); } }

int fdpipe[2];  // Pipe condivisa con i worker per il reinserimento dei fd dei client serviti
pthread_mutex_t lock_clienticonnessi = PTHREAD_MUTEX_INITIALIZER;
int clienticonnessi = 0;
intqueue_t* fdqueue = NULL;
storage_t* cache = NULL;
pthread_mutex_t lock_log = PTHREAD_MUTEX_INITIALIZER;
FILE* logfile = NULL;

// Restituisce un buffer allocato
char* read_pathname(int fd){
    int n = 0;
    if ( read(fd, &n, sizeof(int)) == -1 )
        return NULL;
    char* pathname = malloc(n);
    memset(pathname, 0, n);
    if ( readn(fd, pathname, n) == -1 )
        return NULL;

    return pathname;
}

// Chiude un fd e resetta i file da esso aperti e lockati
void closeFDRoutine(int fd);

void closeFDRoutine(int fd){
    // TODO
    /*
    lockFdLock();
    fdCleaner_t* tmp = findPerFd(fd);
    if ( tmp ){
        // Il client ha attualmente aperto o lockato alcuni file
        while ( tmp->fileLocked->len > 0 ){
            char* pathname = pop(tmp->fileLocked);
            int newLocker = 0;
            S_unlockFile(cache, pathname, fd, &newLocker); // Non mi interessa il valore di ritorno
            // Sistemo con la lock
            if ( newLocker ){
                int err = 0;
                // Sblocco un client che era in attesa di prendere la lock
                if ( write(newLocker, &err, sizeof(int)) == -1 ){
                    closeFDRoutine(newLocker);
                }
            }
            // free(pathname); Non lo libero, perché è un puntatore in comune con le altre strutture
        }
        while ( tmp->fileOpened->len > 0 ){
            char* pathname = pop(tmp->fileOpened);
            closePerFD(cache, pathname, fd);
            // free(pathname); Non lo libero, perché è un puntatore in comune con le altre strutture
        }
    }
    lockFdUnlock();
    */
    if ( pthread_mutex_lock(&lock_clienticonnessi) == -1 ){
        perror("pthread_mutex_lock");
        exit(-1);
    }
    clienticonnessi--;
    assert(clienticonnessi>=0);
    if ( pthread_mutex_unlock(&lock_clienticonnessi) == -1 ){
        perror("pthread_mutex_unlock");
        exit(-1);
    }
    close(fd);
}

void* Worker(void* args){
    if ( DEBUG) printf("WORKER: Sono attivo\n");
    
    int fd = -1;
    request_t* req = malloc(sizeof(request_t));
    int err = -1;
    int okPush = 0;

    while ( (fd = dequeue(fdqueue)) != -1 ){
        if ( DEBUG ) printIntQueue(fdqueue);
        okPush = 1; // Salvo diverse modifiche
        memset(req, 0, sizeof(request_t));

        if ( readn(fd, req, sizeof(request_t)) == -1)
            fprintf(stderr, "WORKER: Errore leggendo un messaggio da un client\n");
        else
            if ( DEBUG) printf("\nWORKER: Leggo un messaggio dal file descriptor %d\n\n", fd);

        switch(req->op){
            case( OPEN_FILE ):{
                char* pathname = read_pathname(fd);
                int enqueued = 0;

                if ( req->flags_N & OCREAT ){
                    if ( DEBUG ) printf("WORKER: Il client %d ha richiesto una openFile con OCREAT\n", fd);
                    err = S_createFile(cache, pathname, fd) ? errno : 0;
                    if ( err ){
                        if ( DEBUG) printf("WORKER: S_createFile fallita con errore - %s\n", strerror(err));
                        WRITE_LOG("Errore o forse no\n");
                        if ( write(fd, &err, sizeof(int)) == -1 ){
                            closeFDRoutine(fd);
                            okPush = 0;
                        }
                        break;
                    }
                    else if ( DEBUG ) printf("WORKER: S_createFile eseguita con successo\n");
                }

                // Apro il file
                if ( DEBUG ) printf("WORKER: Il client %d ha richiesto una openFile\n", fd);
                err = S_openFile(cache, pathname, fd) ? errno: 0;
                if ( err ){
                    if ( DEBUG) printf("WORKER: S_openFile fallita con errore - %s\n", strerror(err));
                    WRITE_LOG("Errore o forse no\n");
                    if ( write(fd, &err, sizeof(int)) == -1 ){
                        closeFDRoutine(fd);
                        okPush = 0;
                    }
                    break;
                }
                else if ( DEBUG ) printf("WORKER: S_openFile eseguita con successo\n");

                if ( req->flags_N & OLOCK ){
                    if ( DEBUG ) printf("WORKER: Il client %d ha richiesto una opneFile con OLOCK\n", fd);
                    err = S_lockFile(cache, pathname, fd, &enqueued) ? errno : 0;
                    if ( err ){
                        if ( DEBUG) printf("WORKER: S_lockFile fallita con errore - %s\n", strerror(err));
                        WRITE_LOG("Errore o forse no");
                        if ( write(fd, &err, sizeof(int)) == -1 ){
                            closeFDRoutine(fd);
                            okPush = 0;
                        }
                        break;
                    }
                    else if ( DEBUG ) printf("WORKER: S_lockFile eseguita con successo, non so ancora se il client ha ottenuto la lock\n");

                    if ( !enqueued ){
                        /* 
                            Posso rispondere al client, 
                            altrimenti lo lascio in sospeso sulla read finché
                            - un altro client non rilascia la lock e può ottenerla
                            - il file non viene rimosso
                        */
                        if ( write(fd, &err, sizeof(int)) == -1 ){
                            closeFDRoutine(fd);
                            okPush = 0;
                            break;
                        }
                        if ( DEBUG ) printf("WORKER: Il client %d ha ottenuto la lock sul file\n", fd);
                        WRITE_LOG("Errore o forse no\n");
                    }
                    // Caso in cui il client è in coda ad attendere la lock
                }
                else{
                    // Caso in cui è stato richiesta una openFile senza OLOCK o in cui il client ha ottenuto la lock
                    // Posso rispondere al client
                    if ( write(fd, &err, sizeof(int)) == -1 ){
                        closeFDRoutine(fd);
                        okPush = 0;
                        break;
                    }
                    if ( DEBUG ) printf("WORKER: openFile eseguita con successo con i flag %d\n", req->flags_N);
                    WRITE_LOG("Errore o forse no\n");
                }

                if ( DEBUG ){
                    printf("WORKER: La prima openFile va a buon fine, perché le operazioni successive no?\n");
                    printCache(cache);
                    printf("WORKER: Spero la stampa possa chiarire\n");
                }
            } break;
            case( CLOSE_FILE ):{
                char* pathname = read_pathname(fd);
                
                if ( DEBUG ) printf("WORKER: Il client %d ha richiesto una closeFile\n", fd);
                errno = 0;
                err = ( S_closeFile(cache, pathname) ) ? errno : 0;
                if ( err ){
                    if ( DEBUG) printf("WORKER: S_closeFile fallita con errore - %s\n", strerror(err));
                    WRITE_LOG("Errore o forse no\n");
                }
                else
                    if ( DEBUG ) printf("WORKER: S_closeFile eseguita con successo\n");
                
                if ( write(fd, &err, sizeof(int)) == -1 ){
                    closeFDRoutine(fd);
                    okPush = 0;
                    break;
                }
            } break;
            case( WRITE_FILE ):{
                char* pathname = read_pathname(fd);

                size_t size = 0;
                
                if ( DEBUG ) printf("WORKER: Il client %d ha richiesto una writeFile\n", fd);
                if ( DEBUG ) printf("WORKER: Leggo quanti bytes mi sta per inviare il client\n");
                if ( readn(fd, &size, sizeof(size_t)) == -1 ){
                    closeFDRoutine(fd);
                    okPush = 0;
                    break;
                }
                char* buf = malloc(size);
                memset(buf, 0, size);
                if ( DEBUG ) printf("WORKER: Sto per leggere %ld bytes dal client\n", size);
                if ( readn(fd, buf, size) == -1 ){
                    closeFDRoutine(fd);
                    okPush = 0;
                    break;
                }
                if ( DEBUG ) printf("WORKER: Ho ricevuto il contenuto \"%s\" dal client, inoltro l'operazione allo Storage\n", buf);

                Queue_t* victim = initQueue();
                int len = 0;
                
                err = ( S_uploadFile(cache, pathname, fd, buf, size, victim, &len) ) ? errno : 0;
                if ( err ){
                    if ( DEBUG) printf("WORKER: S_uploadFile fallita con errore - %s\n", strerror(err));
                    WRITE_LOG("Errore o forse no\n");
                    // break? No, non entro nel ciclo del cache miss perché len è 0
                }
                else if ( DEBUG ) printf("WORKER: S_uploadFile eseguita con successo\n");
                // Rispondo con l'esito
                if ( write(fd, &err, sizeof(int)) == -1 ){
                    closeFDRoutine(fd);
                    okPush = 0;
                    // Niente break per avvisare chi era fermo sulla lock
                }
                // Invio al client i file espulsi dalla cache (in else per evitare un doppio errore sulla write)
                else if ( write(fd, &len, sizeof(int)) == -1 ){
                    closeFDRoutine(fd);
                    okPush = 0;
                    // break? No, devo comunque avvisare i client in attesa della lock su quel file
                }

                for ( int i = 0; i < len; i++ ){
                    size_t n = -1;
                    file_t* vic = pop(victim);
                    n = strlen(vic->pathname)+1;
                    
                    // Avviso i client bloccati sulla lock che il file non c'è (più)
                    err = ENOENT;
                    for ( int j = 0; j < vic->maxfd; j++){
                        if ( FD_ISSET(j, &vic->lockWait) )
                            // Sblocco un client che era in attesa di prendere la lock
                            if ( write(i, &err, sizeof(int)) == -1 )
                                closeFDRoutine(i);
                            
                        // Per i client che avevano semplicemente aperto il file non faccio niente
                        // quando faranno delle operazioni vedranno rispondersi ENOENT
                    }
                    // Invio la dimensione del nome del file
                    if ( writen(fd, &n, sizeof(size_t)) == -1 ){
                        closeFDRoutine(fd);
                        okPush = 0;
                        break;
                    }
                    // Invio il nome del file
                    if ( writen(fd, vic->pathname, n) == -1 ){
                        closeFDRoutine(fd);
                        okPush = 0;
                        break;
                    }
                    // Invio la dimensione del contenuto del file
                    if ( writen(fd, &vic->size, sizeof(size_t)) == -1 ){
                        closeFDRoutine(fd);
                        okPush = 0;
                        break;
                    }
                    // Invio il contenuto del file
                    if ( writen(fd, vic->content, vic->size) == -1 ){
                        closeFDRoutine(fd);
                        okPush = 0;
                        break;
                    }
                    
                    // Libero il file
                    free(vic->content);
                    free(vic->pathname);
                    free(vic);
                }

                deleteQueue(victim);
                free(victim);
            } break;
            case ( APPEND_FILE):{
                char* pathname = read_pathname(fd);

                size_t size = 0;
                
                if ( readn(fd, &size, sizeof(size_t)) == -1 ){
                    closeFDRoutine(fd);
                    okPush = 0;
                    break;
                }
                char* buf = malloc(size);
                memset(buf, 0, size);
                if ( readn(fd, buf, size) == -1 ){
                    closeFDRoutine(fd);
                    okPush = 0;
                    break;
                }

                Queue_t* victim = initQueue();
                int len = 0;
                
                err = ( S_appendFile(cache, pathname, fd, victim, buf, size, &len) ) ? errno : 0;
                if ( err ){
                    if ( DEBUG) printf("WORKER: S_writeFile fallita con errore - %s\n", strerror(err));
                    WRITE_LOG("Errore o forse no\n");
                    okPush = 1;
                    break;
                }
                // Rispondo con l'esito
                if ( write(fd, &err, sizeof(int)) == -1 ){
                    closeFDRoutine(fd);
                    okPush = 0;
                    // Niente break, mi serve un else
                }
                // Invio al client i file espulsi dalla cache
                else if ( write(fd, &len, sizeof(int)) == -1 ){
                    closeFDRoutine(fd);
                    okPush = 0;
                    // break; Devo comunque avvisare i client in attesa della lock su quel file
                }
                for ( int i = 0; i < len; i++ ){
                    size_t n = -1;
                    file_t* vic = pop(victim);
                    n = strlen(vic->pathname)+1;

                    // Avviso i client bloccati sulla lock che il file non c'è (più)
                    err = ENOENT;
                    for ( int j = 0; j < vic->maxfd; j++){
                        if ( FD_ISSET(j, &vic->lockWait) )
                            // Sblocco un client che era in attesa di prendere la lock
                            if ( write(i, &err, sizeof(int)) == -1 )
                                closeFDRoutine(i);
                            
                        // Per i client che avevano semplicemente aperto il file non faccio niente
                        // quando faranno delle operazioni vedranno rispondersi ENOENT
                    }
                    // Invio la dimensione del nome del file
                    if ( writen(fd, &n, sizeof(size_t)) == -1 ){
                        closeFDRoutine(fd);
                        okPush = 0;
                        break;
                    }
                    // Invio il nome del file
                    if ( writen(fd, vic->pathname, n) == -1 ){
                        closeFDRoutine(fd);
                        okPush = 0;
                        break;
                    }
                    // Invio la dimensione del contenuto del file
                    if ( writen(fd, &vic->size, sizeof(size_t)) == -1 ){
                        closeFDRoutine(fd);
                        okPush = 0;
                        break;
                    }
                    // Invio il contenuto del file
                    if ( writen(fd, vic->content, vic->size) == -1 ){
                        closeFDRoutine(fd);
                        okPush = 0;
                        break;
                    }
                    // Libero il file
                    free(vic->content);
                    free(vic->pathname);
                    free(vic);
                }

                deleteQueue(victim);
                free(victim);
            } break;
            case ( READ_FILE ):{
                char* pathname = read_pathname(fd);
                void* buf = NULL;
                size_t size;

                if ( DEBUG ) printf("WORKER: Il client %d ha richiesto una readFile\n", fd);
                err = S_readFile(cache, pathname, fd, &buf, &size) ? errno : 0;
                if ( err ){
                    if ( DEBUG) printf("WORKER: S_readFile fallita con errore - %s\n", strerror(err));
                    WRITE_LOG("Errore o forse no\n");
                    if ( write(fd, &err, sizeof(int)) == -1 ){
                        closeFDRoutine(fd);
                        okPush = 0;
                        break;
                    }
                    break;
                }
                else if ( DEBUG ) printf("WORKER: S_readFile eseguita con successo\n");
                // Rispondo che l'operazione è andata a buon fine e invio il contenuto
                if ( write(fd, &err, sizeof(int)) == -1 ){
                    closeFDRoutine(fd);
                    okPush = 0;
                    break;
                }
                // Invio la size
                if ( writen(fd, &size, sizeof(size_t)) == -1 ){
                    closeFDRoutine(fd);
                    okPush = 0;
                    break;
                }
                // Invio il contenuto

                printf("\nWORKER: Sto inviando il contenuto %s al CLIENT\n\n", (char*)buf);

                if ( writen(fd, buf, size) == -1 ){
                    closeFDRoutine(fd);
                    okPush = 0;
                    break;
                }
                
            } break;
            case ( READ_N_FILES):{
                Queue_t* read = initQueue();
                int read_size = 0;

                err = S_readNFiles(cache, fd, req->flags_N, read, &read_size) ? errno : 0;
                if ( err ){
                    if ( DEBUG) printf("WORKER: S_closeFile fallita con errore - %s\n", strerror(err));
                    WRITE_LOG("Errore o forse no\n");
                    if ( write(fd, &err, sizeof(int)) == -1 ){
                        closeFDRoutine(fd);
                        okPush = 0;
                    }
                    break;
                }
                // Altrimenti risposta positiva
                if ( write(fd, &err, sizeof(int)) == -1 ){
                    closeFDRoutine(fd);
                    okPush = 0;
                    break;
                }
                // Comunico quanti file deve leggere
                if ( write(fd, &read_size, sizeof(int)) == -1 ){
                    closeFDRoutine(fd);
                    okPush = 0;
                    break;
                }

                for ( int i = 0; i < read_size; i++ ){
                    file_t* tmp = pop(read);
                    int n = strlen(tmp->pathname)+1;
                    // Invio la dimensione del nome del file
                    if ( writen(fd, &n, sizeof(size_t)) == -1 ){
                        closeFDRoutine(fd);
                        okPush = 0;
                        break;
                    }
                    // Invio il nome del file
                    if ( writen(fd, tmp->pathname, n) == -1 ){
                        closeFDRoutine(fd);
                        okPush = 0;
                        break;
                    }
                    // Invio la dimensione del contenuto del file
                    if ( writen(fd, &tmp->size, sizeof(size_t)) == -1 ){
                        closeFDRoutine(fd);
                        okPush = 0;
                        break;
                    }
                    if ( writen(fd, tmp->content, tmp->size) == -1 ){
                        closeFDRoutine(fd);
                        okPush = 0;
                        break;
                    }
                    free(tmp);
                }

                deleteQueue(read);
                free(read);
            } break;
            case ( LOCK_FILE ):{
                char* pathname = read_pathname(fd);
                int enqueued = 0;
                
                err = ( S_lockFile(cache, pathname, fd, &enqueued) ) ? errno : 0;
                if ( err ){
                    if ( DEBUG) printf("WORKER: S_lockFile fallita con errore - %s\n", strerror(err));
                    WRITE_LOG("Errore o forse no\n");
                    break;
                }
                if ( err || !enqueued )
                    if ( write(fd, &err, sizeof(int)) == -1 ){
                        closeFDRoutine(fd);
                        okPush = 0;
                        break;
                    }
                // Altrimenti non rispondo e lascio il client in sospeso sulla read
                
            } break;
            case ( UNLOCK_FILE ):{
                char* pathname = read_pathname(fd);
                int newLocker = 0;
                
                err = ( S_unlockFile(cache, pathname, fd, &newLocker) ) ? errno : 0;
                if ( err ){
                    if ( DEBUG) printf("WORKER: S_unlockFile fallita con errore - %s\n", strerror(err));
                    WRITE_LOG("Errore o forse no\n");
                    break;
                }

                if ( newLocker )
                    // Sblocco un client che era in attesa di prendere la lock
                    if ( write(newLocker, &err, sizeof(int)) == -1 )
                        closeFDRoutine(newLocker);

                // Rispondo al client che ha richiesto la unlock
                if ( write(fd, &err, sizeof(int)) == -1 ){
                    closeFDRoutine(fd);
                    okPush = 0;
                    break;
                }

            } break;
            case ( REMOVE_FILE ):{
                char* pathname = read_pathname(fd);
                int maxfd = 0;
                fd_set toStop;
                FD_ZERO(&toStop);
                
                err = ( S_removeFile(cache, pathname, fd, &toStop, &maxfd) ) ? errno : 0;
                if ( err ){
                    if ( DEBUG) printf("WORKER: S_removeFile fallita con errore - %s\n", strerror(err));
                    WRITE_LOG("Errore o forse no\n");
                    if ( write(fd, &err, sizeof(int)) == -1 ){
                        closeFDRoutine(fd);
                        okPush = 0;
                    }
                    break;
                }
                // Rispondo al client che ha richiesto la remove
                if ( write(fd, &err, sizeof(int)) == -1 ){
                    closeFDRoutine(fd);
                    okPush = 0;
                    break;
                }
                err = ENOENT; // Il file è stato rimosso
                // Sblocco tutti i client fermi sulla read in attesa della lock
                for ( int i = 0; i < maxfd; i++ ){
                    if ( FD_ISSET(i, &toStop) )
                        // Sblocco un client che era in attesa di prendere la lock
                        if ( write(i, &err, sizeof(int)) == -1 )
                            closeFDRoutine(fd);
                }
            } break;
            case ( CLOSE_CONNECTION ):{
                /*
                    Il client identificato con fd ha chiuso la connessione,
                    devo toglierlo da tutti i file che aveva aperto o bloccato
                */
                closeFDRoutine(fd);
                if ( DEBUG ) printf("WORKER: closeConnection eseguita con successo se non viene reinserito il fd nel readset\n");

                okPush = 0; // Non reinserisco il fd nella coda dei client da ascoltare
            } break;
            default:{
                ;
            }
        }

        if ( okPush ){
            // Reinserisco il fd del client appena servito nella pipe
            if ( write(fdpipe[1], &fd, sizeof(int)) == -1 ) {
                perror("Write");
                fprintf(stderr, "WORKER: Errore scrivendo sulla pipe\n");
                exit(-1);
            }
            if (DEBUG) printf("WORKER: Rimando al server il fd %d\n", fd);
        }
        fd = -1;
    }

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
        return -1;
    }
    signal(SIGPIPE, SIG_IGN); // Ignoro sigpipe

    mode_t oldmask = umask(033);
    logfile = fopen(filestat, "w+");
    umask(oldmask);
    if ( !logfile ){
        fprintf(stderr, "SERVER: Errore nell'apertura del file di logging\n");
        return -1;
    }

    fdqueue = createQ();
    if ( !fdqueue ) {
        fprintf(stderr, "SERVER: Errore nella creazione delle coda dei file descriptor\n");
        return -1;
    }

    cache = S_createStorage(max_file, max_storage);
    if ( !cache ){
        fprintf(stderr, "SERVER: Errore nella creazione della cache\n");
        return -1;
    }

    pthread_t worker[max_worker];
    for ( int i = 0; i < max_worker; i++ )
        if ( pthread_create(&worker[i], NULL, Worker, NULL) != 0 ){
            perror("Pthread_create");
            return -1;
        }
    
    // Apro la connessione socket
    int listenfd = openServerConnection(filesocketname); // Socket su cui fare la accept
    if ( listenfd == -1 ){
        fprintf(stderr, "SERVER: Errore nella creazione della connessione socket\n");
        return -1;
    }

    int fdmax, tmpfdmax;
    fd_set readset, tmpset;
    // Azzero sia il readset che il set temporaneo usato per la select
    FD_ZERO(&readset);
    FD_ZERO(&tmpset);
    // Aggiungo il listenfd fd al readset
    FD_SET(listenfd, &readset);
    // Creo le pipe
    if ( pipe(fdpipe) == -1 || pipe(sigpipe) == -1 ){
        perror("Pipe");
        return -1;
    }
    // Aggiungo le pipe al readset
    FD_SET(fdpipe[0], &readset);
    FD_SET(sigpipe[0], &readset);

    // Assegno a fdmax il fd maggiore fra quelli attualmente nel readset
    if ( DEBUG ) printf("SERVER: Scelgo il massimo tra %d, %d e %d\n", sigpipe[0], fdpipe[0], listenfd);
    fdmax = max(sigpipe[0], fdpipe[0], listenfd);
    if ( DEBUG ) printf("SERVER: fdmax vale %d\n", fdmax);

    // Il server accetta nuove connessioni e smista le richeste dei client fino all'arrivo di un segnale di chiusura
    if ( pthread_mutex_lock(&lock_clienticonnessi) != 0 ){
        perror("Pthread_mutex_lock");
        return -1;
    }
    while ( !ragequit && ( !softquit || clienticonnessi ) ){
        if ( pthread_mutex_unlock(&lock_clienticonnessi) != 0 ){
            perror("Pthread_mutex_unlock");
            return -1;
        }
        tmpset = readset;

        if ( DEBUG ) printf("SERVER: fdmax vale %d\n", fdmax);
        if ( select(fdmax+1, &tmpset, NULL, NULL, NULL) == -1 ){
            if ( errno != EINTR ){
                perror("Select");
                return -1;
            }
            else
                // Sono stato interrotto da un segnale
                // Riprendo e attendo di essere avvisato dalla sigpipe per applicare la giusta politica di uscita
                printf("SERVER: Sono stato interrotto da un segnale, ci sono ancora %d client connessi\n", clienticonnessi);
        }
            
        if ( ragequit )
            goto quit;

        tmpfdmax = fdmax;
        for ( int i = tmpfdmax; i > 0; i-- )
            if ( FD_ISSET(i, &tmpset) ){
                if ( DEBUG ) printf("SERVER: Mi domando quale sia l'ultimo indice che visito (%d)\n", i);
                if ( i == sigpipe[0] ){
                    printf("SERVER: In chiusura...\n");
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
                        printf("SERVER: Il segnale ricevuto era SIGHUP,"
                        " ci sono %d clienti connessi\n", clienticonnessi);
                        // Chiudo il listenfd e continuo a servire richieste finché ci sono ancora client connessi
                        FD_CLR(listenfd, &readset);

                        // Aggiorno fdmax se ce n'è bisogno
                        if ( i == fdmax )
                            for ( int i = (fdmax-1); i >= 0; --i )
                                if ( FD_ISSET(i, &readset) )
                                    fdmax = i;

                        close(listenfd);
                        if ( pthread_mutex_lock(&lock_clienticonnessi) != 0 ){
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
                    fdmax = (connfd > fdmax ) ? connfd : fdmax;// max(fdmax, connfd);

                    FD_SET(connfd, &readset);
                    if ( pthread_mutex_lock(&lock_clienticonnessi) != 0 ){
                        perror("Pthread_mutex_lock");
                        return -1;
                    }
                    clienticonnessi++;
                    if ( pthread_mutex_unlock(&lock_clienticonnessi) != 0 ){
                        perror("Pthread_mutex_unlock");
                        return -1;
                    }
                }
                else if ( i == fdpipe[0] ){
                    // Reinserisco il fd contenuto in fdpipe nel readset
                    int connfd; // fd del client da reinserire
                    if ( read(fdpipe[0], &connfd, sizeof(int)) == -1 ){
                        perror("Read");
                        return -1;
                    }
                    if ( DEBUG ) printf("SERVER: Reinserisco tra i fd da ascoltare il %d\n", connfd);
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
                        if ( pthread_mutex_lock(&lock_clienticonnessi) != 0 ){
                            perror("Pthread_mutex_lock");
                            return -1;
                        }
                        clienticonnessi--;
                        if ( pthread_mutex_unlock(&lock_clienticonnessi) != 0 ){
                            perror("Pthread_mutex_unlock");
                            return -1;
                        }
                        close(i);
                        continue;
                    }
                }
            }
    }
    
    // Ho la lock su clienticonnessi
    // Se tutti i clienti hanno chiuso la connessione posso procedere alla terminazione del server
    if ( softquit && clienticonnessi == 0 ){
        if ( pthread_mutex_unlock(&lock_clienticonnessi) != 0 ){
            perror("Pthread_mutex_unlock");
            return -1;
        }

        goto quit;
    }

    quit: {
        // Chiudo tutti gli fd, invio un segnale di chiusura ai worker e attendo la loro terminazione
        int termfd = -1;
        for ( int i = 0; i < max_worker; i++ )
            enqueue(fdqueue, termfd);
        
        for ( int i = 0; i < fdmax; i++ ){
            FD_CLR(i, &readset);
            // Chiudo senza interessarmi della corretta terminazione dei client
            close(i);
        }

        for ( int i = 0; i < max_worker; i++ )
            pthread_join(worker[i], NULL);

    };

    deleteQ(fdqueue);
    free(fdqueue);
    free(filestat);
    free(filesocketname);
    return 0;
}
    for ( int i = 0; i < max_worker; i++ )
        if ( pthread_create(&worker[i], NULL, Worker, NULL) != 0 ){
            perror("Pthread_create");
            return -1;
        }
    
    // Apro la connessione socket
    int listenfd = openServerConnection(filesocketname); // Socket su cui fare la accept
    if ( listenfd == -1 ){
        fprintf(stderr, "FILE STORAGE: Errore nella creazione della connessione socket\n");
        return -1;
    }

    int fdmax, tmpfdmax;
    fd_set readset, tmpset;
    // Azzero sia il readset che il set temporaneo usato per la select
    FD_ZERO(&readset);
    FD_ZERO(&tmpset);
    // Aggiungo il listenfd fd al readset
    FD_SET(listenfd, &readset);
    // Creo le pipe
    if ( pipe(fdpipe) == -1 || pipe(sigpipe) == -1 ){
        perror("Pipe");
        return -1;
    }
    // Aggiungo le pipe al readset
    FD_SET(fdpipe[0], &readset);
    FD_SET(sigpipe[0], &readset);

    // Assegno a fdmax il fd maggiore fra quelli attualmente nel readset
    fdmax = ( sigpipe[0] > listenfd ) ? sigpipe[0] : listenfd;
    fdmax = ( fdmax > fdpipe[0] ) ? fdmax : fdpipe[0];

    // Il server accetta nuove connessioni e smista le richeste dei client fino all'arrivo di un segnale di chiusura
    if ( pthread_mutex_lock(&lock_clienticonnessi) != 0 ){
        perror("Pthread_mutex_lock");
        return -1;
    }
    while ( !ragequit && ( !softquit || clienticonnessi ) ){
        if ( pthread_mutex_unlock(&lock_clienticonnessi) != 0 ){
            perror("Pthread_mutex_unlock");
            return -1;
        }
        tmpset = readset;

        if ( select(fdmax+1, &tmpset, NULL, NULL, NULL) == -1 ){
            if ( errno != EINTR ){
                perror("Select");
                return -1;
            }
            else
                // Sono stato interrotto da un segnale
                // Riprendo e attendo di essere avvisato dalla sigpipe per applicare la giusta politica di uscita
                printf("FILE STORAGE: Sono stato interrotto da un segnale\n");
        }
            
        if ( ragequit )
            goto quit;

        tmpfdmax = fdmax;
        for ( int i = tmpfdmax; i > 0; i-- )
            if ( FD_ISSET(i, &tmpset) ){
                if ( i == sigpipe[0] ){
                    printf("FILE STORAGE: In chiusura...\n");
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
                        printf("FILE STORAGE: Il segnale ricevuto era SIGHUP,"
                        " ci sono %d clienti connessi\n", clienticonnessi);
                        // Chiudo il listenfd e continuo a servire richieste finché ci sono ancora client connessi
                        FD_CLR(listenfd, &readset);

                        // Aggiorno fdmax se ce n'è bisogno
                        if ( i == fdmax )
                            for ( int i = (fdmax-1); i >= 0; --i )
                                if ( FD_ISSET(i, &readset) )
                                    fdmax = i;

                        close(listenfd);
                        if ( pthread_mutex_lock(&lock_clienticonnessi) != 0 ){
                            perror("Pthread_mutex_lock");
                            return -1;
                        }
                        break; // Evito la accept sul listenfd e riparto dalla select
                    }
                }
                else if ( i == listenfd ){
                    // Nuova connessione da parte di un client
                    int connfd; // fd del nuovo client da inserire nel readset
                    if ( ( connfd = accept(listenfd, NULL, NULL ) ) == -1 ){
                        perror("Accept");
                        return -1;
                    }
                    if ( connfd > fdmax )
                        fdmax = connfd;

                    FD_SET(connfd, &readset);
                    if ( pthread_mutex_lock(&lock_clienticonnessi) != 0 ){
                        perror("Pthread_mutex_lock");
                        return -1;
                    }
                    clienticonnessi++;
                    if ( pthread_mutex_unlock(&lock_clienticonnessi) != 0 ){
                        perror("Pthread_mutex_unlock");
                        return -1;
                    }
                }
                else if ( i == fdpipe[0] ){
                    // Reinserisco il fd contenuto in fdpipe nel readset
                    int connfd; // fd del client da reinserire
                    if ( read(fdpipe[0], &connfd, sizeof(int)) == -1 ){
                        perror("Read");
                        return -1;
                    }

                    FD_SET(connfd, &readset);
                    if ( fdmax < connfd)
                        fdmax = connfd;
                }
                else{
                    // È una nuova richiesta da un client già connesso
                    // Devo pushare i nella coda dei fd da servire
                    FD_CLR(i, &readset);
                    
                    // Aggiorno fdmax se ce n'è bisogno
                    if ( i == fdmax )
                        for ( int i = (fdmax-1); i >= 0; --i )
                            if ( FD_ISSET(i, &readset) )
                                fdmax = i;

                    if ( enqueue(fdqueue, i) != 0 ){
                        fprintf(stderr, "FILE STORAGE: Errore nell'inserimento di un nuovo fd in coda\n");
                        if ( pthread_mutex_lock(&lock_clienticonnessi) != 0 ){
                            perror("Pthread_mutex_lock");
                            return -1;
                        }
                        clienticonnessi--;
                        if ( pthread_mutex_unlock(&lock_clienticonnessi) != 0 ){
                            perror("Pthread_mutex_unlock");
                            return -1;
                        }
                        close(i);
                        continue;
                    }
                }
            }
    }
    // Ho la lock su clienticonnessi
    // Se tutti i clienti hanno chiuso la connessione posso procedere alla terminazione del server
    if ( softquit && clienticonnessi == 0 ){
        if ( pthread_mutex_unlock(&lock_clienticonnessi) != 0 ){
            perror("Pthread_mutex_unlock");
            return -1;
        }

        goto quit;
    }

    quit: {
        if ( DEBUG ) printf("SERVER: Sono alla goto di uscita rapida\n");
        // Chiudo tutti gli fd, invio un segnale di chiusura ai worker e attendo la loro terminazione
        // int termfd = -1;
        for ( int i = 0; i < max_worker; i++ )
            enqueue(fdqueue, -1);
        
        if ( DEBUG ) printf("SERVER: Ho inviato un segnale di terminazione ai worker\n");
        
        // Ignoro i file descriptor di stdout, stdin, stderr e quelli dei worker
        for ( int i = 3 + max_worker; i < fdmax; i++ ){
            if ( DEBUG ) printf("SERVER: Tolgo il file descriptor %d dal readset\n", i);
            FD_CLR(i, &readset);
            // Chiudo senza interessarmi della corretta terminazione dei client
            close(i);
            if ( DEBUG ) printf("SERVER: Ho chiuso il file descriptor %d\n", i);
        }

        if ( DEBUG ) printf("SERVER: Ho chiuso tutte le connessioni con i client\n");

        for ( int i = 0; i < max_worker; i++ )
            pthread_join(worker[i], NULL);

        if ( DEBUG ) printf("SERVER: Tutti i worker sono terminati correttamente\n");

    };

    fclose(logfile);

    deleteStorage(cache);

    deleteQ(fdqueue);
    free(fdqueue);
    free(filestat);
    free(filesocketname);
    return 0;
}
