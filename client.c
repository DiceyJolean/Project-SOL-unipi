// Erica Pistolesi 518169

#if !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE  600
#endif

#ifndef DEBUG
#define DEBUG 0
#endif

#ifndef MILLION
#define MILLION 1000000
#endif

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>

#include "api.h"
#include "queue.h"

static int pid = 0;

static void print_usage(char* programname){
    printf("%s: Use:\n"
    "   -h : Visualizza questo messaggio\n"
    "   -f <filename> : Si connette al socket AF_UNIX passato\n"
    "   -p : Abilita le stampe sullo standard output\n"
    "   -t <time> : Tempo da attendere tra due operazioni (in millisecondi)\n"
    "   -r <file1>[,file2] : Richiede di leggere i file passati\n"
    "   -R [n=0] : Richiede di leggere al più n file (se n non è specificato richiede tutti i file)\n"
    "   -d <dirname> : Specifica la directory dove salvare i file letti\n"
    "   -w <dirname>[,n=0] : Invia da scrivere al server gli al più n file presenti nella directory passata e nelle sue sottodirectory (se n non è specificato invia tutti i file)\n"
    "   -W <file1>[,file1] : Invia da scrivere al server i file passati\n"
    "   -D <dirname> : Specifica la directory dove salvare i file espulsi dal server\n"
    "   -l <file1>[,file2] : Richiede la lock sui file passati\n"
    "   -u <file1>[,file2] : Rilascia la lock sui file passati\n"
    "   -c <file1>[,file2] : Richiede di rimuovere i file passati\n\n"
    , programname);
}

// Funzione che ricorsivamente legge al più n file nella cartella dirname e nelle sue sottocartelle e li inserisce in q
int fileToWrite(int n, char* dirname, Queue_t* q);

int fileToWrite(int n, char* dirname, Queue_t* q){
    int visited = 0, left = 0;
    char* path = NULL;
    struct stat info;
    
    chdir(dirname);
    DIR* dir = opendir(".");
    if ( !dir ){
        errno = EINVAL;
        return -1;
    }
    struct dirent *file;

    while ( ( errno = 0, file = readdir(dir) ) != NULL ){
        if ( n > 0 && visited >= n )
            break;

        if ( !file )
            return -1; // errno già settato da readdir

        if ( stat(file->d_name, &info) == -1 )
            return -1;

        int len = strlen(file->d_name) + strlen(dirname) +2;
        char* path = malloc(len);
        memset(path, 0, len);
        snprintf(path, len, "%s/%s", dirname, file->d_name);

        if ( S_ISDIR(info.st_mode) ){
            // Evito loop sulla directory corrente e sulla directory padre
            if ( strcmp(file->d_name, ".") != 0 && strcmp(file->d_name, "..") != 0 ){    
            // file è una directory, la visito ricorsivamente       

                left = ( n < 0 ) ? n : n - visited;
                int v = fileToWrite(left, path, q);
                if ( v != -1 )
                    visited += v;
                
            }
            free(path);
        }
        else{
            // file è un file
            if ( DEBUG ) printf("fileToWrite: Trovo il file %s da mettere in coda\n", path);

            if ( push(q, path) == 0 )
                visited++;

            // free(path); il path sarà poi il filename nello storage, libera lui lo spazio
        }
    }

    closedir(dir);

    return visited;
}

// Funzione che salva in dirname il file rinominato filename con contenuto content
int saveFileInDir(char* filename, char* dirname, void* content){
    if ( !filename )
        return -1;

    if ( !dirname ){
        if ( DEBUG ) printf("saveFile: Non è stata specificata la cartella dove salvare i file\n");
        return -1;
    }

    char* tmp = strrchr(filename, '/'); // Salvo soltanto il nome del file senza il path assoluto
    char* filenamecpy = ( tmp ) ? my_strcpy(tmp+1) : my_strcpy (filename);
    if ( !filenamecpy )
        return -1;
    char* dirnamecpy = my_strcpy(dirname);
    if ( !dirnamecpy ){
        free(filenamecpy);
        return -1;
    }
        
    // Per prima cosa creo la directory dove salvare i file
    // Mi salvo la directory corrente per spostarmi
    char* currentdir = malloc(MAX_PATHNAME*sizeof(char));
    getcwd(currentdir, MAX_PATHNAME);
    if ( !currentdir){
        free(dirnamecpy);
        free(filenamecpy);
        return -1;
    }
    
    // Creo la directory
    if ( my_mkdirP(dirnamecpy) != 0 ) {
        if ( DEBUG ) printf("saveFileInDir: Errore nella creazione di %s\n", dirnamecpy);
        free(dirnamecpy);
        free(currentdir);
        free(filenamecpy);
        return -1;
    }
    // Mi sposto nella directory dove devo salvare i file
    chdir(dirnamecpy);
    if ( DEBUG ) printf("saveFileInDir: Mi sono spostato nella directory %s\n", dirnamecpy);

    if ( DEBUG ) printf("saveFileInDir: Salvo il file %s nella cartella %s\n", filenamecpy, dirname);

    mode_t oldmask = umask(033);
    // Creo il file dove scrivere il contenuto letto
    FILE* file = fopen(filenamecpy, "w+");
    if ( !file ){
        if ( DEBUG ) printf("saveFileInDir: Creazione di %s fallita - %s\n", filenamecpy, strerror(errno));
        chdir(currentdir);
        free(filenamecpy);
        free(dirnamecpy);
        free(currentdir);

        return -1;
    }
    umask(oldmask);
    if ( content ){
        // Se necessario, scrivo il contenuto nel file
        fputs(content, file);
        fputs("\n", file);
    }
    fclose(file);
    
    chdir(currentdir);

    free(filenamecpy);
    free(dirnamecpy);
    free(currentdir);

    return 0;
}

int main(int argc, char* argv[]){

    if ( argc < 3 ){
        // Non è stato indicato nemmeno il file per la connessione socket
        print_usage(argv[0]);
        errno = EINVAL;
        return -1;
    }

    char* programname = strdup(argv[0]); // Nome del programma
    pid = (int)getpid(); // ID del processo
    struct timespec attesa = {0, 0};    // Tempo da attendere fra un'operazione e l'altra
    int p = 0; // Flag che indica se le stampe sono abilitate o no
    int t = 0; // Millisecondi da attendere tra un'operazione e l'altra
    char *socketname = NULL; // Nome della socket per la connessione con il server
    int lastOp = -1; // Ultima operazione eseguita, necessario ricordarla nel caso di -d e -D
    int connected = 0; // Flag che indica se il client è connesso
    int count = 0; // Conta il numero di token visitati
    int esito = 0;
    int err = 0;

    int opt = 0;
    const char* optstring = "h:f:p::t:r:R:d:w:W:D:l:u:c:";
    while( (opt = getopt(argc, argv, optstring) ) != -1 ){
        nanosleep(&attesa, NULL); // Attendo tra un comando e l'altro
        count++;
        switch(opt){
            case 'h':{
                // Stampa il messaggio di help e termina
                print_usage(programname);
                free(programname);
                if ( socketname ) free(socketname);
                return 0;
            }
            case 't':{
                if ( optarg == NULL ){
                    // Errore se non ho specificato il tempo di attesa
                    print_usage(programname);
                    free(programname);
                    if ( socketname ) free(socketname);
                    return -1;
                }
                count++;
                t = strtol(optarg, NULL, 10);
                if ( errno == ERANGE || errno == EINVAL ){
                    // Valore invalido per il tempo di attesa
                    perror("strtol");
                    print_usage(programname);
                    free(programname);
                    if ( socketname ) free(socketname);
                    return -1;
                }
                attesa.tv_nsec = t*MILLION;
                attesa.tv_sec = 0;
                if (p) printf("CLIENT %d: Il tempo di attesa tra le richieste al server è di %d millisecondi\n", pid, t);
                lastOp = 't';
            } break;
            case 'p':{
                if ( p ){
                    // p può essere specificata soltanto una volta
                    errno = EPERM;
                    fprintf(stderr, "CLIENT %d: Operazione non consentita - %s\n", pid, strerror(errno));
                    free(programname);
                    if ( socketname ) free(socketname);
                    return -1;
                }
                p = 1;
                lastOp = 'p';
            } break;
            case 'f':{
                if ( connected ){
                    // La connessione era già stata stabilita
                    if ( p ) printf("CLIENT %d: Sono già connesso\n", pid);
                    closeConnection(socketname);
                    free(programname);
                    free(socketname);
                    return -1;
                }
                if ( optarg == NULL ){
                    // Non è stato specificato il file socket
                    print_usage(programname);
                    closeConnection(socketname);
                    free(programname);
                    if ( socketname ) free(socketname);
                    return -1;
                }
                count++;
                socketname = strdup(optarg);
                
                struct timespec abstime;
                clock_gettime(CLOCK_REALTIME, &abstime);
                abstime.tv_sec += 30; // Aspetto al massimo trenta secondi prima di terminare per connettermi
                // TODO
                
                int delay = 3; // Tempo che lascio intercorrere tra un tentativo di connessione e l'altro
                if ( openConnection(socketname, delay, abstime) != 0){
                    if ( p ) fprintf(stderr, "CLIENT %d: openConnection fallita - %s\n", pid, strerror(errno));
                    free(programname);
                    if ( socketname ) free(socketname);
                    return -1;
                }
                connected = 1; // Connessione stabilita correttamente
                
                if ( p ) printf("CLIENT %d: openConnection(%s, %d, %lld.%.9ld) eseguita con successo\n", pid, socketname, 10, (long long)abstime.tv_sec,abstime.tv_nsec);
                
                lastOp = 'f';
            } break;
            case 'r':{
                if ( !connected ){
                    if ( p ) printf("CLIENT %d: Non sono connesso al server\n", pid);
                    free(programname);
                    closeConnection(socketname);
                    if ( socketname ) free(socketname);
                    return -1;
                }
                if ( !optarg ){
                    if ( p ) printf("CLIENT %d: Parametri errati nella chiamata readFile\n", pid);
                    continue;
                }
                char* token = strdup(optarg), *temp = NULL;
                count++;

                char* dir_letti = NULL; // Directory dove salvare i file letti
                // Controllo se è stata indicata la cartella dove salvare i file letti
                // if ( DEBUG ) printf("CLIENT %d: Controllo se è stata indicata la cartella dove salvare i file letti [count=%d], [argv(%d)=%s]\n", pid, count, count+2, argv[count+2]);
                if ( count+2 < argc ){
                    // if ( DEBUG ) printf("__ argv[%d] contiene %s, infatti il primo carattere è %c e il secondo è %c\n", count+1, argv[count+1], argv[count+1][0], argv[count+1][1]);
                    if ( argv[count+1][0] == '-' && argv[count+1][1] == 'd' ){
                        dir_letti = malloc(strlen(argv[count+2])+1);
                        strncpy(dir_letti, argv[count+2], strlen(argv[count+2])+1);
                    }
                }

                if ( p ){
                    if ( dir_letti ) printf("CLIENT %d: La cartella per i file %s è %s\n", pid, token, dir_letti);
                    else printf("CLIENT %d: Non è stata speficificata la cartella dove salvare i file %s\n", pid, token);
                }
                // Prelevo il primo file dal token
                strtok_r(token, ",", &temp);

                do{
                    char* file = my_strcpy(token); // Nome del file
                    void* buf = NULL; // Buffer dove salvare il contenuto del file

                    size_t size = 0;
                    errno = 0;
                    // Per prima cosa richiedo un'apertura del file
                    esito = openFile(file, 0);
                    err = errno;
                    if ( p ) {
                        if ( esito != 0 ){
                            printf("CLIENT %d: openFile(%s) fallita - %s\n", pid, file, strerror(err));
                            free(file);
                            memmove(token, temp, strlen(temp)+1);
                            strtok_r(token, ",", &temp);
                            continue;
                        }
                        else printf("CLIENT %d: openFile(%s) eseguita con successo\n", pid, file);
                    }

                    errno = 0;
                    // Il file è aperto, richiedo la lettura
                    esito = readFile(file, &buf, &size);
                    err = errno;
                    if ( p ){
                        if ( esito == 0 ) printf("CLIENT %d: readFile(%s) eseguita con successo\n", pid, file);
                        else printf("CLIENT %d: readFile fallita - %s\n", pid, strerror(err));
                    }

                    if ( DEBUG ) printf("CLIENT %d: Ho ricevuto dal SERVER il file %s contentente %s\n", pid, file, (char*)buf);

                    // Se è stata indicata la cartella dove salvare il file, proseguo alla creazione su disco
                    if ( dir_letti )
                        saveFileInDir(file, dir_letti, buf );
                    
                    errno = 0;
                    // Infine chiudo il file
                    esito = closeFile(file);
                    err = errno;
                    if ( p ){
                        if ( esito != 0 ) printf("CLIENT %d: closeFile(%s) fallita - %s %d\n", pid, file, strerror(err), err);
                        else printf("CLIENT %d: closeFile(%s) eseguita con successo\n", pid, file);
                    }
                    
                    // Leggo il prossimo file passato come argomento a -r e proseguo finché non li ho letti tutti
                    memmove(token, temp, strlen(temp)+1);
                    strtok_r(token, ",", &temp);

                    free(file);
                    free(buf);
                } while ( strcmp(token, "") != 0 );

                if ( dir_letti ) free(dir_letti);
                free(token);
                lastOp = 'r';
            } break;
            case 'R':{
                if ( !connected ){
                    if ( p ) printf("CLIENT %d: Non sono connesso al server\n", pid);
                    closeConnection(socketname);
                    return -1;
                }
                int n = -1; // File richiesti da leggere

                if ( optarg ){
                    count++;

                    // Controllo se è stato passato il parametro n=x
                    char* token = strdup(optarg);
                    char* tmp = strrchr(token, '=');
                    if ( !tmp ){
                        if ( p ) fprintf(stderr, "CLIENT %d: Parametri errati nella chiamata ReadNFiles (token[0] - %c token[1] - %c)\n", pid, token[0], token[1]);
                        free(token);
                        continue;
                    }

                    tmp++;
                    // Salvo il valore numerico di n
                    n = strtol(tmp, NULL, 10);
                    if ( errno == ERANGE || errno == EINVAL ){
                        if ( p ) fprintf(stderr, "CLIENT %d: Parametri errati nella chiamata ReadNFiles)\n", pid);
                        free(token);
                        continue;
                    }
                    free(token);
                }

                char* dir_letti = NULL; // Directory dove salvare i file letti
                // Controllo se è stata indicata la cartella dove salvare i file letti
                if ( count+2 < argc )
                    if ( argv[count+1][0] == '-' && argv[count+1][1] == 'd' ){
                        dir_letti = malloc(strlen(argv[count+2])+1);
                        strncpy(dir_letti, argv[count+2], strlen(argv[count+2])+1);
                    }

                if ( p ){
                    if ( dir_letti ) printf("CLIENT %d: La cartella per i file letti è %s\n", pid, dir_letti);
                    else printf("CLIENT %d: Non è stata speficificata la cartella dove salvare i file letti\n", pid);
                }

                errno = 0;
                // Eseguo la readNFiles
                int esito = readNFiles(n, dir_letti);
                int err = errno;
                if ( p ){
                    if ( esito == 0 ) printf("CLIENT %d: Eseguito con successo readNFiles(%d, %s)\n", pid, n, dir_letti);
                    else printf("CLIENT %d: ReadNFiles fallita - %s\n", pid, strerror(err));
                    // TODO
                }

                if ( dir_letti) free(dir_letti);
                lastOp = 'R';
            } break;
            case 'w':{
                if ( !connected ){
                    if ( p ) printf("CLIENT %d: Non sono connesso al server\n", pid);
                    free(programname);
                    if ( socketname ) free(socketname);
                    return -1;
                }
                if ( !optarg ){
                    if ( p ) fprintf(stderr, "CLIENT %d: Parametri errati nella chiamata writeFile\n", pid);
                    continue;
                }
                
                char* dirname = strdup(optarg);
                count++;
                int n = -1;
                char* token = strrchr(dirname, ',');
                if (token) {
                    n = strtol(token+3, NULL, 10);
                    if ( errno == ERANGE || errno == EINVAL ){
                        if ( p ) fprintf(stderr, "CLIENT %d: Parametri errati nella chiamata writeFile\n", pid);
                        free(dirname);
                        continue;
                    }
                    *token = '\0'; // Lascio solo il nome della directory e tolgo la n
                }
                
                char* dir_espulsi = NULL; // Directory dove salvare i file espulsi
                if ( count+2 < argc )
                    if ( argv[count+1][0] == '-' && argv[count+1][1] == 'D' ){
                        dir_espulsi = malloc(strlen(argv[count+2])+1);
                        strncpy(dir_espulsi, argv[count+2], strlen(argv[count+2])+1);
                    }

                // Inizio a visitare ricorsivamente dirname

                Queue_t *q = initQueue();
                if ( DEBUG ) printf("CLIENT %d: Richiedo di scrivere %d file presenti nella cartella %s\n", pid, n, dirname);


                char* currentdir = malloc(MAX_PATHNAME*sizeof(char));
                getcwd(currentdir, MAX_PATHNAME);
                if ( !currentdir){
                    // TODO 
                }
                // Numero di file che devo inviare al server
                int nfile = fileToWrite(n, dirname, q);
                chdir(currentdir);
                free(currentdir);
                
                if ( DEBUG ) printf("CLIENT %d: Ho trovato %d file da spedire al server\n", pid, nfile);
                for ( int i = 0; i < nfile; i++ ){
                    // Invio gli nfile file che ho trovato in dirname al server
                    char* filename = pop(q);
                    if ( DEBUG ) printf("CLIENT %d: Richiedo di scrivere il file %s\n", pid, filename);

                    // Eseguo la scrittura del file nel server con la lock
                    errno = 0;
                    esito = openFile(filename, OCREAT|OLOCK);
                    err = errno;
                    if ( esito != 0 ){
                        if ( DEBUG ) printf("CLIENT %d: openFile con OCREAT fallita - %s\n", pid, strerror(err));
                        if ( err == EEXIST ){
                            // Se il file esiste già mi limito ad aprirlo
                            errno = 0;
                            esito = openFile(filename, OLOCK);
                            err = errno;
                            if ( p ){
                                if ( esito != 0 ){
                                    printf("CLIENT %d: openFile fallita - %s\n", pid, strerror(err));
                                    free(filename);
                                    continue;
                                }
                                else printf("CLIENT %d: openFile(%s) eseguita con successo\n", pid, filename);
                            }
                        }
                        else{
                            if ( p ) printf("CLIENT %d: openFile con OCREAT fallita - %s\n", pid, strerror(err));
                            break;
                        }
                    }
                    if ( p ) printf("CLIENT %d: openFile(%s) eseguita con successo\n", pid, filename);

                    errno = 0;
                    esito = writeFile(filename, dir_espulsi);
                    err = errno;
                    if ( p ){
                        if ( esito == 0 ) printf("CLIENT %d: writeFile(%s) eseguita con successo\n", pid, filename);
                        else printf("CLIENT %d: writeFile fallita - %s\n", pid, strerror(err));
                    }

                    // Se la writeFile fallisce con EPERM provo a scrivere in append
                    if ( err == EPERM ){
                        if ( DEBUG ) printf("CLIENT %d: Provo a eseguire una appendToFile visto che la writeFile non è permessa\n", pid);
                        errno = 0;

                        int fdfile = open(filename, O_RDONLY);
                        if ( fdfile == -1 ){
                            free(programname);
                            closeConnection(socketname);
                            free(socketname);
                            return -1; // errno già settato dalla open
                        }

                        struct stat info;
                        if ( stat(filename, &info) == -1 ){
                            free(programname);
                            closeConnection(socketname);
                            free(socketname);
                            return -1; // errno già settato dalla stat
                        }
                        
                        size_t size = info.st_size;
                        char* buf = malloc(size);
                        memset(buf, 0, size);
                        read(fdfile, buf, size);
                        close(fdfile);

                        esito = appendToFile(filename, buf, size, dir_espulsi);
                        err = errno;
                        if ( p ){
                            if ( esito ) printf("CLIENT %d: appendToFile fallita - %s\n", pid, strerror(err));
                            else printf("CLIENT %d: appendToFile(%s) eseguita con successo\n", pid, filename);
                        }

                        free(buf);
                    }

                    if ( DEBUG ) printf("CLIENT %d: Richiedo una unlockFile\n", pid);        
                    errno = 0;
                    int esito = unlockFile(filename);
                    int err = errno;
                    if ( p ){
                        if ( esito == 0 ) printf("CLIENT %d: eseguita con successo unlockFile(%s)\n", pid, filename);
                        else printf("CLIENT %d: unlockFile fallita - %s\n", pid, strerror(err));
                    }

                    errno = 0;
                    esito = closeFile(filename);
                    err = errno;
                    if ( p ){
                        if ( esito ) printf("CLIENT %d: closeFile fallita - %s\n", pid, strerror(err));
                        else printf("CLIENT %d: closeFile(%s) eseguita con successo\n", pid, filename);
                    }
                }

                free(dirname);
                lastOp = 'w';
            } break;
            case 'W':{
                if ( !connected ){
                    if ( p ) printf("CLIENT %d: Non sono connesso al server\n", pid);
                    free(programname);
                    if ( socketname ) free(socketname);
                    return -1;
                }
                if ( !optarg ){
                    if ( p ) printf("CLIENT %d: Parametri errati nella chiamata writeFile\n", pid);
                    continue;
                }
                char* token = strdup(optarg), *temp = NULL;
                count++;
                char* dir_espulsi = NULL; // Directory dove salvare i file espulsi
    
                // Controllo se è stata indicata la cartella dove salvare i file espulsi
                if ( count+2 < argc )
                    if ( argv[count+1][0] == '-' && argv[count+1][1] == 'D' ){
                        dir_espulsi = malloc(strlen(argv[count+2])+1);
                        strncpy(dir_espulsi, argv[count+2], strlen(argv[count+2])+1);
                    }

                if ( p  ){
                    if ( dir_espulsi ) printf("CLIENT %d: La cartella per i file espulsi è %s\n", pid, dir_espulsi);
                    else printf("CLIENT %d: Non è stata speficificata la cartella dove salvare i file espulsi\n", pid);
                }
                strtok_r(token, ",", &temp);

                do{                    
                    errno = 0;
                    esito = openFile(token, OCREAT|OLOCK);
                    err = errno;
                    if ( esito != 0 ){
                        if ( err == EEXIST ){
                            errno = 0;
                            esito = openFile(token, OLOCK);
                            err = errno;
                            if ( p )
                                if ( esito != 0 ){
                                    printf("CLIENT %d: openFile fallita - %s\n", pid, strerror(err));
                                    break;
                                }
                        }
                        else{
                            if ( p ) printf("CLIENT %d: openFile fallita - %s\n", pid, strerror(err));
                            break;
                        }
                    }
                    if ( p ) printf("CLIENT %d: openFile(%s) eseguita con successo\n", pid, token);
                    errno = 0;
                    esito = writeFile(token, dir_espulsi);
                    err = errno;
                    if ( p ){
                        if ( esito == 0 ) printf("CLIENT %d: writeFile(%s) eseguita con successo\n", pid, token);
                        else printf("CLIENT %d: writeFile fallita - %s\n", pid, strerror(err));
                    }

                    // Se la writeFile fallisce con EPERM si prova a fare la appendFile
                    if ( err == EPERM ){
                        if ( DEBUG ) printf("CLIENT %d: Provo a eseguire una appendToFile visto che la writeFile non è permessa\n", pid);
                        errno = 0;

                        int fdfile = open(token, O_RDONLY);
                        if ( fdfile == -1 ){
                            free(programname);
                            closeConnection(socketname);
                            free(socketname);
                            return -1; // errno già settato dalla open
                        }

                        struct stat info;
                        if ( stat(token, &info) == -1 ){
                            free(programname);
                            closeConnection(socketname);
                            free(socketname);
                            return -1; // errno già settato dalla stat
                        }
                        
                        size_t size = info.st_size;
                        char* buf = malloc(size);
                        memset(buf, 0, size);
                        read(fdfile, buf, size);
                        close(fdfile);

                        esito = appendToFile(token, buf, size, dir_espulsi);
                        err = errno;
                        if ( p ){
                            if ( esito ) printf("CLIENT %d: appendToFile fallita - %s\n", pid, strerror(err));
                            else printf("CLIENT %d: appendToFile(%s) eseguita con successo\n", pid, token);
                        }

                        free(buf);
                    }
                    
                    if ( DEBUG ) printf("CLIENT %d: Richiedo una unlockFile\n", pid);        
                    errno = 0;
                    int esito = unlockFile(token);
                    int err = errno;
                    if ( p ){
                        if ( esito == 0 ) printf("CLIENT %d: eseguita con successo unlockFile(%s)\n", pid, token);
                        else printf("CLIENT %d: unlockFile fallita - %s\n", pid, strerror(err));
                    }

                    errno = 0;
                    esito = closeFile(token);
                    err = errno;
                    if ( p ){
                        if ( esito ) printf("CLIENT %d: closeFile fallita - %s\n", pid, strerror(err));
                        else printf("CLIENT %d: closeFile(%s) eseguita con successo\n", pid, token);
                    }
                    
                    memmove(token, temp, strlen(temp)+1);
                    strtok_r(token, ",", &temp);
                    
                } while ( strcmp(token, "") != 0 );

                free(token);
                lastOp = 'W';
            } break;
            case 'l':{
                if ( !connected ){
                    if ( p ) printf("CLIENT %d: Non sono connesso al server\n", pid);
                    free(programname);
                    if ( socketname ) free(socketname);
                    return -1;
                }
                if ( !optarg ){
                    if ( p ) printf("CLIENT %d: Parametri errati nella chiamata lockFile\n", pid);
                    continue;
                }
                char* token = strdup(optarg), *temp = NULL;
                
                strtok_r(token, ",", &temp);

                do{                    
                    errno = 0;
                    int esito = lockFile(token);
                    int err = errno;
                    if ( p ){
                        if ( esito == 0 ) printf("CLIENT %d: eseguita con successo lockFile(%s)\n", pid, token);
                        else printf("CLIENT %d: lockFile fallita - %s\n", pid, strerror(err));
                    }

                    memmove(token, temp, strlen(temp)+1);
                    strtok_r(token, ",", &temp);
                    
                } while ( strcmp(token, "") != 0 );

                free(token);
                lastOp = 'l';
            } break;
            case 'u':{
                if ( !connected ){
                    if ( p ) printf("CLIENT %d: Non sono connesso al server\n", pid);
                    free(programname);
                    if ( socketname ) free(socketname);
                    return -1;
                }
                if ( !optarg ){
                    if ( p ) printf("CLIENT %d: Parametri errati nella chiamata unlockFile\n", pid);
                    continue;
                }
                char* token = strdup(optarg), *temp = NULL;
                
                strtok_r(token, ",", &temp);

                do{           
                    if ( DEBUG ) printf("CLIENT %d: Richiedo una unlockFile\n", pid);         
                    errno = 0;
                    int esito = unlockFile(token);
                    int err = errno;
                    if ( p ){
                        if ( esito == 0 ) printf("CLIENT %d: eseguita con successo unlockFile(%s)\n", pid, token);
                        else printf("CLIENT %d: unlockFile fallita - %s\n", pid, strerror(err));
                    }

                    memmove(token, temp, strlen(temp)+1);
                    strtok_r(token, ",", &temp);
                    
                } while ( strcmp(token, "") != 0 );

                free(token);
                lastOp = 'u';
            } break;
            case 'c':{
                if ( !connected ){
                    if ( p ) printf("CLIENT %d: Non sono connesso al server\n", pid);
                    free(programname);
                    if ( socketname ) free(socketname);
                    return -1;
                }
                if ( !optarg ){
                    if ( p ) printf("CLIENT %d: Parametri errati nella chiamata removeFile\n", pid);
                    continue;
                }
                char* token = strdup(optarg), *temp = NULL;
                
                strtok_r(token, ",", &temp);

                do{                    
                    errno = 0;
                    int esito = removeFile(token);
                    int err = errno;
                    if ( p ){
                        if ( esito == 0 ) printf("CLIENT %d: eseguita con successo removeFile(%s)\n", pid, token);
                        else printf("CLIENT %d: removeFile fallita - %s\n", pid, strerror(err));
                    }

                    memmove(token, temp, strlen(temp)+1);
                    strtok_r(token, ",", &temp);
                    
                } while ( strcmp(token, "") != 0 );

                free(token);
                lastOp = 'c';
            } break;
            case 'd':{
                if ( optarg == NULL ){
                    print_usage(programname);
                    free(programname);
                    closeConnection(socketname);
                    if ( socketname ) free(socketname);
                    return -1;
                }
                count++;
                
                if ( lastOp == 'r' || lastOp == 'R' )
                    continue;
                
                fprintf(stderr, "CLIENT %d: È stata specificata la cartella dove salvare i file in lettura, ma non è stata richiesta nessuna lettura\n", pid);
            }   break;
            case 'D':{
                if ( optarg == NULL ){
                    print_usage(programname);
                    free(programname);
                    closeConnection(socketname);
                    if ( socketname ) free(socketname);
                    return -1;
                }
                count++;
                
                if ( lastOp == 'W' || lastOp == 'w' )
                    continue;

                fprintf(stderr, "CLIENT %d: È stata specificata la cartella dove salvare i file espulsi dal server, ma non è stata richiesta nessuna scrittura\n", pid);
            }
        }
    }

    closeConnection(socketname);
    free(socketname);
    free(programname);

    if ( DEBUG ) printf("CLIENT %d: Terminazione con successo\n", pid);

    return 0;
}
