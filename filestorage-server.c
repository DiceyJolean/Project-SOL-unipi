#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/select.h>

#include "serverutility.h"
#include "storage.h"

int fdpipe[2];  // Pipe condivisa con i worker per il reinserimento dei fd dei client serviti
pthread_mutex_t lock_clienticonnessi = PTHREAD_MUTEX_INITIALIZER;
int clienticonnessi = 0;
intqueue_t* fdqueue;

void* Worker(void* args){
    printf("WORKER: Sono attivo\n");
    
    

    return NULL;
}

int main(int argc, char* argv[]) {

    // Setto i parametri dal file di configurazione
    if ( set_config(argc, argv) != 0 )
        return 0;

    // Installo il signal handler per i segnali da gestire
    if ( installSigHand() != 0 ) {
        fprintf(stderr, "FILE STORAGE: Errore nell'installazione del signal handler\n");
        return -1;
    }

    fdqueue = createQ();
    if ( !fdqueue ) {
        fprintf(stderr, "FILE STORAGE: Errore nella creazione delle coda dei file descriptor\n");
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
