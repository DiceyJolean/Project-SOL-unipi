
#ifndef SERVER_UTILITY_H
#define SERVER_UTILITY_H

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <stdarg.h>
#include <sys/un.h>

#include "icl_hash.h"

#define MAX_CHAR_LENGHT 1024
#define MAX_FILE "max file"
#define MAX_WORKER "max worker"
#define MAX_STORAGE "max storage"
#define FILE_SOCKET "file socket"
#define FILE_STAT "file stat"

long max_storage = 0;
int max_file = 0, max_worker = 0; 
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
    
    if (dim != 3 || ( strncmp(stream[1], "-f", 2) != 0 ) ) {
        fprintf(stderr, "FILE STORAGE:\n"
                "   Chiamare il processo indicando il path per il file di configurazione con il flag -f\n");
        return -1;
    }

    char *arg = strdup(stream[2]);
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
        } 
        else if (strncmp(token, MAX_STORAGE, strlen((MAX_STORAGE))) == 0) {
            token = strtok_r(tmpstr, "\n", &tmpstr);
            max_storage = strtol(token, NULL, 10);
        } 
        else if (strncmp(token, MAX_WORKER, strlen(MAX_WORKER)) == 0) {
            token = strtok_r(tmpstr, "\n", &tmpstr);
            max_worker = strtol(token, NULL, 10);
        } 
        else if (strncmp(token, FILE_SOCKET, strlen(FILE_SOCKET)) == 0) {
            token = strtok_r(tmpstr, "\n", &tmpstr);
            filesocketname = strdup(token);
        }
        else if (strncmp(token, FILE_STAT, strlen(FILE_STAT)) == 0) {
            token = strtok_r(tmpstr, "\n", &tmpstr);
            filestat = strdup(token);
        }
        else {
            fprintf(stderr, "Not valid argument in %s\n", arg);
            return -1;
        }
    }
    fclose(config);

    if (max_file <= 0 || max_worker <= 0 || max_storage <= 0 || filesocketname == NULL || filestat == NULL ) {
        fprintf(stderr, "Not valid argument in %s\n", arg);
        return -1;
    }
    printf("Configurazione del file storage server:\n"
           "    Memoria dello storage : %ld\n"
           "    Numero massimo di file presenti nel server : %d\n"
           "    Numero di workers totali nel server : %d\n"
           "    File socket per la comunicazione con i client : %s\n"
           "    File per il logging delle operazioni effettuate : %s\n\n",
           max_storage, max_file, max_worker, filesocketname, filestat);

    free(arg);
    return 0;
}

int sigpipe[2];
// Uscita immediata
volatile sig_atomic_t ragequit = 0;
// Uscita gentile
volatile sig_atomic_t softquit = 0;

void sighandler(int sig){
    switch(sig){
        case SIGINT:
        case SIGQUIT:{
            char* buf = "\nRicevuto SIGQUIT o SIGINT\n\0";
            write(STDOUT_FILENO, buf, strlen(buf)+1);
            write(sigpipe[1], &sig, sizeof(int));
            ragequit = 1;
        } break;
        case SIGHUP:{
            char* buf = "\nRicevuto SIGHUP\n\0";
            write(STDOUT_FILENO, buf, strlen(buf)+1);
            write(sigpipe[1], &sig, sizeof(int));
            softquit = 1;
        } break;
        default:{
            char* buf = "\nRicevo un segnale che non appartiene alla maschera\n\0";
            write(STDOUT_FILENO, buf, strlen(buf)+1);
            abort();
        }
    }
}

/**
 * @brief Installazione del gestore dei segnali per SIGQUIT, SIGHUP e SIGINT (ignora SIGPIPE)
 */
int installSigHand(){

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sighandler;
    sigset_t handlermask;
    memset(&handlermask, 0, sizeof(handlermask));
    sa.sa_mask = handlermask;

    sigemptyset(&handlermask);
    sigaddset(&handlermask, SIGHUP);
    sigaddset(&handlermask, SIGQUIT);
    sigaddset(&handlermask, SIGINT);

    if ( sigaction(SIGHUP, &sa, NULL) == -1 ){
        perror("sigaction SIGHUP");
        return -1;
    }
    if ( sigaction(SIGQUIT, &sa, NULL) == -1 ){
        perror("sigaction SIGQUIT");
        return -1;
    }
    if ( sigaction(SIGINT, &sa, NULL) == -1 ){
        perror("sigaction SIGHUP");
        return -1;
    }

    return 0;
}

/**
 * @brief Crea la socket per la comunicazione lato server
 *
 * @return Il file descriptor riferito al socket per accettare le richieste in arrivo, -1 in caso di fallimento
 */
int openServerConnection(char* filesocketname){

    unlink(filesocketname);

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

int max(int first, ...){
    va_list arg;
    va_start(arg, first);

    int max = first;
    int next = va_arg(arg, int);
    printf("Valuto next %d\n", next);
    while ( next ){
        max = ( next > max ) ? next : max;
        next = va_arg(arg, int);
    }
    va_end(arg);

    return max;
}

#endif //SERVER_UTILITY_H
