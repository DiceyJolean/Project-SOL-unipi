
#ifndef SERVER_UTILITY_H
#define SERVER_UTILITY_H

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>

#define MAX_CHAR_LENGHT 1024
#define MAX_FILE "max file"
#define MAX_WORKER "max worker"
#define MAX_STORAGE "max storage"
#define FILE_SOCKET "file socket"
#define FILE_STAT "file stat"

long max_file = 0, max_storage = 0, max_worker = 0;
char* filesocketname = NULL, *filestat = NULL;

/**
 * @brief Assegna un valore alle variabili max_file, max_storage, max_worker, filesocketname e filestat
 * leggendole dal file di configurazione indicato tramite -f.
 *
 * @param dim Numero di argomenti da leggere dallo stream
 * @param stream Stream di argomenti, indicare -f nomefileconfigurazione
 * @return 0 in caso di successo, -1 in caso di fallimento
 */
int set_config(int dim, char** stream) {
    if (dim != 3) {
        fprintf(stderr, "FILE STORAGE SERVER:\n"
                "   Chiamare il processo indicando il path per il file di configurazione con il flag -f\n");
        return -1;
    }
    int op;
    char *optstring = "f:";
    while ((op = getopt(dim, stream, optstring)) != -1) {
        if (op == 'f') {
            char *arg = strdup(optarg);
            errno = 0;
            FILE *config;
            char buffer[BUFSIZ];

            if ((config = fopen(arg, "r")) == NULL) {
                perror("fopen");
                return -1;
            }

            // finché ho righe da leggere in config
            while ((fgets(buffer, BUFSIZ + 1, config)) != NULL) {
                // in buffer ci sono le righe di config
                // con la strtok_r faccio il parsing dei token
                char *line = NULL, *tmpstr = NULL, *token = NULL;

                line = buffer;
                tmpstr = buffer;
                token = strtok_r(line, "=", &tmpstr);

                if (strncmp(token, MAX_FILE, strlen((MAX_FILE))) == 0) {
                    token = strtok_r(tmpstr, "\n", &tmpstr);
                    max_file = strtol(token, NULL, 10);
                } else if (strncmp(token, MAX_STORAGE, strlen((MAX_STORAGE))) == 0) {
                    token = strtok_r(tmpstr, "\n", &tmpstr);
                    max_storage = strtol(token, NULL, 10);
                } else if (strncmp(token, MAX_WORKER, strlen(MAX_WORKER)) == 0) {
                    token = strtok_r(tmpstr, "\n", &tmpstr);
                    max_worker = strtol(token, NULL, 10);
                } else if (strncmp(token, FILE_SOCKET, strlen(FILE_SOCKET)) == 0) {
                    token = strtok_r(tmpstr, "\n", &tmpstr);
                    filesocketname = strdup(token);
                }  else if (strncmp(token, FILE_STAT, strlen(FILE_STAT)) == 0) {
                    token = strtok_r(tmpstr, "\n", &tmpstr);
                    filestat = strdup(token);
                } else {
                    fprintf(stderr, "Not valid argument in config.txt\n");
                    return -1;
                }
            }
            fclose(config);
        }
    }

    if (max_file <= 0 || max_worker <= 0 || max_storage <= 0 || filesocketname == NULL || filestat == NULL ) {
        fprintf(stderr, "Not valid argument in %s\n", optarg);
        return -1;
    }
    printf("Configurazione del file storage server:\n"
           "    Memoria dello storage : %ld\n,"
           "    Numero massimo di file presenti nel server : %ld\n,"
           "    Numero di workers totali nel server : %ld\n,"
           "    File socket per la comunicazione con i client : %s\n"
           "    File per il logging delle operazioni effettuate : %s\n",
           max_storage, max_file, max_worker, filesocketname, filestat);

    return 0;
}

sigset_t handlermask;

// Uscita immediata
volatile sig_atomic_t sigquit = 0, sigint = 0;
// Uscita gentile
volatile sig_atomic_t sighup = 0;

void sighandler(int sig){
    switch(sig){
        case SIGQUIT:{
            sigquit = 1;
        } break;
        case SIGHUP:{
            sighup = 1;
        } break;
        case SIGINT:{
            sigint = 1;
        }
        case SIGPIPE:{
            // Ignoro SIGPIPE
        }
        default:{
            abort();
        }
    }
}

/**
 * @brief Installazione del gestore dei segnali per SIGQUIT, SIGHUP e SIGINT (ignora SIGPIPE)
 */
void installSigHand(){

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sighandler;

    sigemptyset(&handlermask);
    sigaddset(&handlermask, SIGHUP);
    sigaddset(&handlermask, SIGQUIT);
    sigaddset(&handlermask, SIGINT);
    sigaddset(&handlermask, SIGPIPE);

    if ( sigaction(SIGHUP, &sa, NULL) == -1 ){
        perror("sigaction SIGHUP");
        exit(EXIT_FAILURE);
    }

    if ( sigaction(SIGQUIT, &sa, NULL) == -1 ){
        perror("sigaction SIGQUIT");
        exit(EXIT_FAILURE);
    }

    if ( sigaction(SIGINT, &sa, NULL) == -1 ){
        perror("sigaction SIGHUP");
        exit(EXIT_FAILURE);
    }

    if ( sigaction(SIGPIPE, &sa, NULL) == -1 ){
        perror("sigaction SIGPIPE");
        exit(EXIT_FAILURE);
    }
}

void cleanup(){
    unlink(filesocketname);
}

/**
 * @brief Crea la socket per la comunicazione lato server
 *
 * @return Il file descriptor riferito al socket per accettare le richieste in arrivo, -1 in caso di fallimento
 */
int openServerConnection(){

    cleanup();
    atexit(cleanup);

    int sockfd;
    if (( sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1){
        perror("Socket");
        return -1;
    }

    struct sockaddr_un address;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path, filesocketname, strlen(filesocketname) +1);

    // assegno l'indirizzo al socket
    if ( bind(sockfd, (struct sockaddr*)&address, sizeof(address)) == -1 ){
        perror("Bind");
        return -1;
    }

    if ( listen(sockfd, 1) == -1 ){
        perror("Listen");
        return -1;
    }

    return sockfd;
}

#endif //SERVER_UTILITY_H
