#if !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE  600
#endif

#ifndef DEBUG
#define DEBUG 1
#endif

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>

#include "api.h"
#include "queue.h"

static int pid = 0;

char* my_strcpy(char* dest){
    int len = (strlen(dest)+1)*sizeof(char);

    char* src = malloc(len);
    memset(src, 0, len);
    strncpy(src, dest, len);

    if ( !src )
        return NULL;

    return src;
}

static void print_usage(char* programname, FILE* std){
    fprintf(std, "%s: Use:\n"
    "   -h : Per visualizzare le operazioni consentite\n"
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
    "   -c <file1>[,file2] : Chiede di chiudere i file passati\n\n"
    , programname);
}

void myFree(void* p){
    if ( p ){
        free(p);
        p = NULL;
    }
}

// TODO
void clientExit(void* p, ...){
    va_list arg;
    va_start(arg, p);

    myFree(p);
    void* new = va_arg(arg, void*);
    while ( new ){
        myFree(new);
        void* new = va_arg(arg, void*);
    }
    va_end(arg);
}

int fileToWrite(int n, char* dirname, Queue_t* q);

int fileToWrite(int n, char* dirname, Queue_t* q){
    int visited = 0, left = 0;
    char* path = NULL;
    struct dirent *file;
    struct stat info;

    chdir(dirname);
    DIR* dir = opendir(".");
    if ( !dir ){
        errno = EINVAL;
        return -1;
    }
    do{
        errno = 0; // readdir setta errno, resetto per discriminare
        file = readdir(dir);
        if ( !file && errno )
            return -1; // errno già settato da readdir

        // Ignoro le directory . e ..
        if ( strcmp(file->d_name, ".") != 0 && strcmp(file->d_name, ".") != 0 ){
            if ( stat(file->d_name, &info) == -1 )
                return -1;

            if ( S_ISDIR(info.st_mode) ){
                // file è una directory, la visito ricorsivamente

                char* path = (char*)malloc((strlen(dirname)+1 + strlen(file->d_name)+1)*sizeof(char)); // Nuovo percorso da visitare
                strcpy(path, dirname);
                strcat(path, "/");
                strcat(path, file->d_name);
        
                left = ( n < 0 ) ? n : n - visited;
                int v = fileToWrite(n - visited, path, q);
                if ( v != -1 ){
                    free(path);
                    visited += v;
                }
            }
            else{
                // file è un file
                char* path = (char*)malloc((strlen(dirname)+1 + strlen(file->d_name)+1)*sizeof(char)); // Nuovo percorso da visitare
                strcpy(path, dirname);
                strcat(path, "/");
                strcat(path, file->d_name);
        
                // path è il file da far scrivere al server
                if ( push(q, path) == 0 )
                    visited++;

                free(path);
            }
        }
    } while ( file && visited < n );

    closedir(dir);
    return visited;
}

// Se non esiste, crea l'albero di directory dirname
int my_mkdirP(char* dirname){
    for (char* p = strchr(dirname + 1, '/'); p; p = strchr(p + 1, '/')) {
        *p = '\0';
        if (mkdir(dirname, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
            if (errno != EEXIST) {
                *p = '/';
                printf("Errore nella creazione di %s - %s\n", dirname, strerror(errno));
                return -1;
            }
        }
        *p = '/';
    }
    if ( DEBUG ) printf("CLIENT %d: Ho creato la directory %s, il path assoluto è %s\n", pid, dirname, realpath(dirname, NULL));
    return 0;
}

int saveFile(char* filename, char* dirname, void* content){
    if ( !filename || !dirname )
        return -1;

    char* filenamecpy = my_strcpy(filename);
    if ( !filenamecpy )
        return -1;
    char* dirnamecpy = my_strcpy(dirname);
    if ( !dirnamecpy )
        return -1;
        
    // Per prima cosa creo la directory dove salvare i file
    // Mi salvo la directory corrente per spostarmi
    char* currentdir = malloc(MAX_PATHNAME*sizeof(char));
    getcwd(currentdir, MAX_PATHNAME);
    if ( !currentdir)
        return -1;
    
    // Creo la directory
    if ( my_mkdirP(dirnamecpy) != 0 ) {
        printf("CLIENT: Non sono riuscito a creare %s\n", dirnamecpy);
        return -1;
    }
    // Mi sposto nella directory dove devo salvare i file
    chdir(dirnamecpy);
    if ( DEBUG ) printf("CLIENT %d: Mi sono spostato nella directory %s\n", pid, dirnamecpy);

    // TODO Ricreo l'albero di directory per ogni file?
    // Per ora salvo solo il file

    if ( DEBUG ) printf("CLIENT %d: Salvo il file %s nella cartella %s\n", pid, filename, dirname);

    /*
    // Creazione dell'albero di directory per il file, non funziona

    // Per ogni file ricreo l'albero delle directory in cui era contenuto
    // E poi ci salvo solo il file con il pathname "pulito"
    char* tmp = strrchr(newtoken, '/'); // Puntatore al nome "pulito" del file
    char* filename;
    if ( tmp != NULL ){
        filename = malloc(strlen(tmp)+1); // Nome "pulito" del file
        memset(filename, 0, strlen(tmp)+1);
        strcpy(filename, tmp);
    }
    else{
        // newtoken è già il nome pulito (not good)
        filename = malloc(strlen(newtoken+1)); // Nome "pulito" del file
        memset(filename, 0, strlen(newtoken+1));
        strcpy(filename, newtoken+1);
    }

    if ( DEBUG ) printf("CLIENT %d: Ho ottenuto %s\n", pid, filename);

    if ( DEBUG ) printf("CLIENT %d: Mi sposto dalla cartella %s alla cartella %s\n", pid, currentdir, newdir);
    if ( tmp ){
        // Se il file era in un albero di directory, lo ricreo
        // Adesso recupero l'albero di directory dove era contenuto il file
        *tmp = '\0'; // Taglio il nome del file dal path assoluto, rimane il percorso della directory
        char* filepath = my_mkdirP(newtoken);
        if ( !filepath )
            return -1;
        
        chdir(filepath);
        if ( DEBUG ) printf("CLIENT %d: Ho creato l'albero di directory %s\n", pid, filepath);
    }
    
    */
    mode_t oldmask = umask(033);
    // Creo finalmente il file dove scrivere il contenuto letto in binario
    FILE* file = fopen(filename, "w+");
    umask(oldmask);
    if ( content ) fputs(content, file); // Il terminatore per i file binari è già stato aggiunto dall'API
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
        print_usage(argv[0], stderr);
        errno = EINVAL;
        return -1;
    }

    char* programname = strdup(argv[0]); // Nome del programma
    pid = (int)getpid(); // ID del processo
    struct timespec attesa = {0, 0};    // Tempo da attendere fra un'operazione e l'altra
    int p = 0; // Flag che indica se le stampe sono abilitate o no
    int t = 0; // Millisecondi da attendere tra un'operazione e l'altra
    char *socketname = NULL; // Nome della socket per la connessione con il server
    int lastOp = -1;
    char* dir_letti = NULL; // Directory dove salvare i file letti
    char* dir_espulsi = NULL; // Directory dove salvare i file espulsi
    int connected = 0; // Flag che indica se il client è connesso
    int count = 0; // Conta il numero di token visitati
    int esito = 0;
    int err = 0;

    int opt = 0;
    const char* optstring = "h:f:p::t:r:R:d:w:W:D:l:u:c";
    while( (opt = getopt(argc, argv, optstring) ) != -1 ){
        nanosleep(&attesa, NULL);
        count++;
        dir_letti = NULL;
        dir_espulsi = NULL;
        switch(opt){
            case 'h':{
                print_usage(programname, stdout);
                clientExit(programname, socketname, dir_letti, dir_espulsi);
                return 0;
            }
            case 't':{
                if ( optarg == NULL ){
                    print_usage(programname, stderr);
                    clientExit(programname, socketname, dir_letti, dir_espulsi);
                    return -1;
                }
                count++;
                t = strtol(optarg, NULL, 10);
                if ( errno == ERANGE || errno == EINVAL ){
                    perror("strtol");
                    print_usage(programname, stderr);
                    clientExit(programname, socketname, dir_letti, dir_espulsi);
                    return -1;
                }
                attesa.tv_nsec = t*1000;
                attesa.tv_sec = 0;
                if (p) printf("CLIENT %d: Il tempo di attesa tra le richieste al server è di %d millisecondi\n", pid, t);
                lastOp = 't';
            } break;
            case 'p':{
                if ( p ){
                    errno = EPERM;
                    fprintf(stderr, "CLIENT %d: Operazione non consentita - %s\n", pid, strerror(errno));
                    return -1;
                }
                p = 1;
                lastOp = 'p';
            } break;
            case 'f':{
                if ( connected ){
                    if ( p ) printf("CLIENT %d: Sono già connesso\n", pid);
                    return -1;
                }
                if ( optarg == NULL ){
                    print_usage(programname, stderr);
                    clientExit(programname, socketname, dir_letti, dir_espulsi);
                    return -1;
                }
                count++;
                socketname = strdup(optarg);
                if ( connected ){
                    if (p) fprintf(stderr, "CLIENT %d: openConnection su %s fallita, la connessione era già stabilita\n", pid, socketname);
                    continue;
                }
                struct timespec abstime;
                clock_gettime(CLOCK_REALTIME, &abstime);
                abstime.tv_sec += 30;
                // TODO
                
                if ( openConnection(socketname, 10, abstime) != 0){
                    fprintf(stderr, "CLIENT %d: openConnection fallita - %s\n", pid, strerror(errno));
                    return -1;
                }
                connected = 1;
                
                if (p) printf("CLIENT %d: openConnection(%s, %d, %lld.%.9ld) eseguita con successo\n", pid, socketname, 10, (long long)abstime.tv_sec,abstime.tv_nsec);
                lastOp = 'f';
            } break;
            case 'r':{
                if ( !connected ){
                    if ( p ) printf("CLIENT %d: Non sono connesso al server\n", pid);
                    return -1;
                }
                if ( !optarg ){
                    if ( p ) printf("CLIENT %d: Parametri errati nella chiamata readFile\n", pid);
                    continue;
                }
                char* token = strdup(optarg), *temp = NULL;
                count++;
                
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
                strtok_r(token, ",", &temp);

                do{
                    void* buf = NULL;
                    size_t size = 0;
                    errno = 0;
                    esito = openFile(token, 0);
                    err = errno;
                    if ( p ) {
                        if ( esito != 0 ) printf("CLIENT %d: openFile fallita - %s\n", pid, strerror(err));
                        else printf("CLIENT %d: openFile(%s) eseguita con successo\n", pid, token);
                    }
                    errno = 0;
                    esito = readFile(token, &buf, &size);
                    err = errno;
                    if ( p ){
                        if ( esito == 0 ) printf("CLIENT %d: readFile(%s) eseguita con successo\n", pid, token);
                        else printf("CLIENT %d: readFile fallita - %s\n", pid, strerror(err));
                    }

                    if ( DEBUG ) printf("CLIENT %d: Ho ricevuto dal SERVER il file %s contentente %s\n", pid, token, (char*)buf);

                    if ( saveFile(token, dir_letti, buf ) ){
                        if ( !dir_letti ){
                            if ( DEBUG ) printf("CLIENT %d: Errore nella creazione della directory %s\n", pid, dir_letti);
                            closeConnection(socketname);
                            return -1;
                        }
                    }

                    errno = 0;
                    esito = closeFile(token);
                    err = errno;
                    if ( p ){
                        if ( esito != 0 ) printf("CLIENT %d: closeFile(%s) fallita - %s\n", pid, token, strerror(err));
                        else printf("CLIENT %d: closeFile(%s) eseguita con successo\n", pid, token);
                    }
                    memmove(token, temp, strlen(temp)+1);
                    strtok_r(token, ",", &temp);
                    free(buf);
                } while ( strcmp(token, "") != 0 );

                free(token);
                lastOp = 'r';
            } break;
            case 'R':{
                if ( !connected ){
                    if ( p ) printf("CLIENT %d: Non sono connesso al server\n", pid);
                    return -1;
                }
                int n = -1;

                if ( optarg ){
                    count++;
                    char* token = strdup(optarg);
                    if ( token[0] != 'n' || token[1] != '=' ){
                        if (p) fprintf(stderr, "CLIENT %d: Parametri errati nella chiamata ReadNFiles (token[0] - %c token[1] - %c)\n", pid, token[0], token[1]);
                        free(token);
                        continue;
                    }
                    n = strtol(token+2, NULL, 10);
                    if ( errno == ERANGE || errno == EINVAL ){
                        if (p) fprintf(stderr, "CLIENT %d: Parametri errati nella chiamata ReadNFiles)\n", pid);
                        free(token);
                        continue;
                    }
                    free(token);
                }

                // Controllo se è stata indicata la cartella dove salvare i file letti
                if ( count+2 < argc )
                    if ( argv[count+1][0] == '-' && argv[count+1][1] == 'd' ){
                        dir_letti = malloc(strlen(argv[count+2])+1);
                        strncpy(dir_letti, argv[count+2], strlen(argv[count+2])+1);
                    }

                if ( dir_letti ) printf("CLIENT %d: La cartella per i file letti è %s\n", pid, dir_letti);
                else printf("CLIENT %d: Non è stata speficificata la cartella dove salvare i file letti\n", pid);

                errno = 0;
                int esito = readNFiles(n, dir_letti);
                int err = errno;
                if ( p ){
                    if ( esito == 0 ) printf("CLIENT %d: Eseguito con successo readNFiles(%d, %s)\n", pid, n, dir_letti);
                    else printf("CLIENT %d: ReadNFiles fallita - %s\n", pid, strerror(err));
                }

                lastOp = 'R';
            } break;
            case 'w':{
                if ( !connected ){
                    if ( p ) printf("CLIENT %d: Non sono connesso al server\n", pid);
                    return -1;
                }
                if ( !optarg ){
                    if (p) fprintf(stderr, "CLIENT %d: Parametri errati nella chiamata writeFile\n", pid);
                    continue;
                }
                char* dirname = strdup(optarg);
                count++;
                int n = -1;
                char* token = strrchr(dirname, ',');
                if (token) {
                    n = strtol(token+2, NULL, 10);
                    if ( errno == ERANGE || errno == EINVAL ){
                        if (p) fprintf(stderr, "CLIENT %d: Parametri errati nella chiamata writeFile\n", pid);
                        free(dirname);
                        continue;
                    }
                    strtok(dirname, ",");
                }

                if ( count+2 < argc )
                    if ( argv[count+1][0] == '-' && argv[count+1][1] == 'D' ){
                        dir_espulsi = malloc(strlen(argv[count+2])+1);
                        strncpy(dir_espulsi, argv[count+2], strlen(argv[count+2])+1);
                    }

                // Inizio a visitare ricorsivamente dirname
                Queue_t *q = initQueue();
                
                int nfile = fileToWrite(n, dirname, q);
                for ( int i = 0; i < nfile; i++ ){
                    char* filename = pop(q);
                    errno = 0;
                    int esito = writeFile(filename, dir_espulsi);
                    int err = errno;
                    if ( p ){
                        if ( esito == 0 ) printf("CLIENT %d: eseguita con successo writeFile(%s)\n", pid, filename);
                        else printf("CLIENT %d: writeFile fallita - %s\n", pid, strerror(err));
                    }
                }

                free(dirname);
                lastOp = 'w';
            } break;
            case 'W':{
                if ( !connected ){
                    if ( p ) printf("CLIENT %d: Non sono connesso al server\n", pid);
                    return -1;
                }
                if ( !optarg ){
                    if ( p ) printf("CLIENT %d: Parametri errati nella chiamata writeFile\n", pid);
                    continue;
                }
                char* token = strdup(optarg), *temp = NULL;
                count++;
                
                // Controllo se è stata indicata la cartella dove salvare i file espulsi
                // if ( DEBUG ) printf("CLIENT %d: Controllo se è stata indicata la cartella dove salvare i file espulsi [count=%d], [argv(%d)=%s]\n", pid, count, count+2, argv[count+2]);
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
                    esito = openFile(token, OCREAT);
                    err = errno;
                    if ( esito != 0 ){
                        if ( err == EEXIST ){
                            errno = 0;
                            esito = openFile(token, 0);
                            err = errno;
                            if ( p )
                                if ( esito != 0 ){
                                    printf("CLIENT %d: openFile fallita - %s\n", pid, strerror(err));
                                    break;
                                }
                        }
                        else{
                            printf("CLIENT %d: openFile fallita - %s\n", pid, strerror(err));
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

                    memmove(token, temp, strlen(temp)+1);
                    strtok_r(token, ",", &temp);
                    
                } while ( strcmp(token, "") != 0 );

                free(token);
                lastOp = 'W';
            } break;
            case 'l':{
                if ( !connected ){
                    if ( p ) printf("CLIENT %d: Non sono connesso al server\n", pid);
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
                    return -1;
                }
                if ( !optarg ){
                    if ( p ) printf("CLIENT %d: Parametri errati nella chiamata unlockFile\n", pid);
                    continue;
                }
                char* token = strdup(optarg), *temp = NULL;
                
                strtok_r(token, ",", &temp);

                do{                    
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
                    return -1;
                }
                if ( !optarg ){
                    if ( p ) printf("CLIENT %d: Parametri errati nella chiamata closeFile\n", pid);
                    continue;
                }
                char* token = strdup(optarg), *temp = NULL;
                
                strtok_r(token, ",", &temp);

                do{                    
                    errno = 0;
                    int esito = lockFile(token);
                    int err = errno;
                    if ( p ){
                        if ( esito == 0 ) printf("CLIENT %d: eseguita con successo closeFile(%s)\n", pid, token);
                        else printf("CLIENT %d: closeFile fallita - %s\n", pid, strerror(err));
                    }

                    memmove(token, temp, strlen(temp)+1);
                    strtok_r(token, ",", &temp);
                    
                } while ( strcmp(token, "") != 0 );

                free(token);
                lastOp = 'c';
            } break;
            case 'd':{
                if ( optarg == NULL ){
                    print_usage(programname, stderr);
                    clientExit(programname, socketname, dir_letti, dir_espulsi);
                    return -1;
                }
                count++;
                
                if ( lastOp == 'r' || lastOp == 'R' )
                    continue;
                
                fprintf(stderr, "CLIENT %d: È stata specificata la cartella dove salvare i file in lettura, ma non è stata richiesta nessuna lettura\n", pid);
            }   break;
            case 'D':{
                if ( optarg == NULL ){
                    print_usage(programname, stderr);
                    clientExit(programname, socketname, dir_letti, dir_espulsi);
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
    free(programname);
    return 0;
}
    myFree(p);
    void* new = va_arg(arg, void*);
    while ( new ){
        myFree(new);
        void* new = va_arg(arg, void*);
    }
    va_end(arg);
}

int fileToWrite(int n, char* dirname, Queue_t* q);

int fileToWrite(int n, char* dirname, Queue_t* q){
    int visited = 0, left = 0;
    char* path = NULL;
    struct dirent *file;
    struct stat info;

    DIR* dir = opendir(dirname);
    if ( !dir ){
        errno = EINVAL;
        return -1;
    }
    do{
        errno = 0; // readdir setta errno, resetto per discriminare
        file = readdir(dir);
        if ( !file && errno )
            return -1; // errno già settato da readdir

        // Ignoro le directory . e ..
        if ( strcmp(file->d_name, ".") != 0 && strcmp(file->d_name, ".") != 0 ){
            if ( stat(file->d_name, &info) == -1 )
                return -1;

            if ( S_ISDIR(info.st_mode) ){
                // file è una directory, la visito ricorsivamente

                char* path = (char*)malloc((strlen(dirname)+1 + strlen(file->d_name)+1)*sizeof(char)); // Nuovo percorso da visitare
                strcpy(path, dirname);
                strcat(path, "/");
                strcat(path, file->d_name);
        
                left = ( n < 0 ) ? n : n - visited;
                int v = fileToWrite(n - visited, path, q);
                if ( v != -1 ){
                    free(path);
                    visited += v;
                }
            }
            else{
                // file è un file
                char* path = (char*)malloc((strlen(dirname)+1 + strlen(file->d_name)+1)*sizeof(char)); // Nuovo percorso da visitare
                strcpy(path, dirname);
                strcat(path, "/");
                strcat(path, file->d_name);
        
                // path è il file da far scrivere al server
                if ( push(q, path) == 0 )
                    visited++;

                free(path);
            }
        }
    } while ( file && visited < n );

    closedir(dir);
    return visited;
}

int main(int argc, char* argv[]){

    if ( argc < 3 ){
        // Non è stato indicato nemmeno il file per la connessione socket
        print_usage(argv[0], stderr);
        errno = EINVAL;
        return -1;
    }

    char* programname = strdup(argv[0]); // Nome del programma
    int pid = (int)getpid(); // ID del processo
    struct timespec attesa = {0, 0};    // Tempo da attendere fra un'operazione e l'altra
    int p = 0; // Flag che indica se le stampe sono abilitate o no
    int t = 0; // Millisecondi da attendere tra un'operazione e l'altra
    char *socketname = NULL; // Nome della socket per la connessione con il server
    int lastOp = -1;
    char* dir_letti = NULL; // Directory dove salvare i file letti
    char* dir_espulsi = NULL; // Directory dove salvare i file espulsi
    int connected = 0; // Flag che indica se il client è connesso
    int count = 1;

    int opt = 0;
    const char* optstring = "h:f:p::t:r:R:d:w:W:D:l:u:c";
    while( (opt = getopt(argc, argv, optstring) ) != -1 ){
        count++;
        switch(opt){
            case 'h':{
                print_usage(programname, stdout);
                clientExit(programname, socketname, dir_letti, dir_espulsi);
                return 0;
            }
            case 't':{
                if ( optarg == NULL ){
                    print_usage(programname, stderr);
                    clientExit(programname, socketname, dir_letti, dir_espulsi);
                    return -1;
                }
                t = strtol(optarg, NULL, 10);
                if ( errno == ERANGE || errno == EINVAL ){
                    perror("strtol");
                    print_usage(programname, stderr);
                    clientExit(programname, socketname, dir_letti, dir_espulsi);
                    return -1;
                }
                attesa.tv_nsec = t*1000;
                attesa.tv_sec = 0;
                if (p) printf("CLIENT %d: Il tempo di attesa tra le richieste al server è di %d millisecondi\n", pid, t);
                lastOp = 't';
            } break;
            case 'p':{
                p = 1;
                lastOp = 'p';
            } break;
            case 'f':{
                if ( optarg == NULL ){
                    print_usage(programname, stderr);
                    clientExit(programname, socketname, dir_letti, dir_espulsi);
                    return -1;
                }
                socketname = strdup(optarg);
                if ( connected ){
                    if (p) fprintf(stderr, "CLIENT %d: openConnection su %s fallita, la connessione era già stabilita\n", pid, socketname);
                    continue;
                }
                struct timespec abstime;
                clock_gettime(CLOCK_REALTIME, &abstime);
                abstime.tv_sec += 30;
                // TODO
                
                if ( openConnection(socketname, 10, abstime) != 0){
                    fprintf(stderr, "CLIENT %d: openConnection fallita - %s\n", pid, strerror(errno));
                    return -1;
                }
                
                if (p) printf("CLIENT %d: openConnection(%s, %d, %lld.%.9ld) eseguita con successo\n", pid, socketname, 10, (long long)abstime.tv_sec,abstime.tv_nsec);
                lastOp = 'f';
            } break;
            case 'r':{
                if ( !optarg ){
                    if ( p ) printf("CLIENT %d: Parametri errati nella chiamata readFile\n", pid);
                    continue;
                }
                char* token = strdup(optarg), *temp = NULL;
                
                // Controllo se è stata indicata la cartella dove salvare i file letti
                if ( count+2 < argc )
                    if ( argv[count+1][0] == '-' && argv[count+1][1] == 'd' ){
                        dir_letti = malloc(strlen(argv[count+2])+1);
                        strncpy(dir_letti, argv[count+2], strlen(argv[count+2])+1);
                    }

                if ( dir_letti ) printf("CLIENT %d: La cartella per i file letti è %s\n", pid, dir_letti);
                else printf("CLIENT %d: Non è stata speficificata la cartella dove salvare i file letti\n", pid);
                void* buf = NULL;
                size_t* size = malloc(sizeof(size_t));
                strtok_r(token, ",", &temp);

                do{
                    buf = NULL;
                    *size = 0;
                    
                    errno = 0;
                    int esito = readFile(token, &buf, size);
                    int err = errno;
                    if ( p ){
                        if ( esito == 0 ) printf("CLIENT %d: eseguita con successo readFile(%s)\n", pid, token);
                        else printf("CLIENT %d: readFile fallita - %s\n", pid, strerror(err));
                    }

                    if ( dir_letti ){
                        // Pathname assoluto del file, comprende la cartella dove andrà salvato
                        char* path = (char*)malloc((strlen(dir_letti)+1 + strlen(token))*sizeof(char));
                        memset(path, 0, (strlen(dir_letti)+1 + strlen(token))*sizeof(char));
                        // Creo il path della directory dove andrà salvato
                        strcpy(path, dir_letti);
                        // E poi ci concateno il path assoluto del file
                        strcat(path, token);

                        // copia del path, mi serve per togliere il nome del file dal path e creare la directory in cui salvarlo
                        char* pathcpy = malloc((strlen(path)+1)*sizeof(char));
                        memset(pathcpy, 0, (strlen(path)+1)*sizeof(char));
                        memcpy(pathcpy, path, strlen(path)+1);
                        char* tmp = strrchr(pathcpy, '/'); // tmp punta al nome del file
                        if (tmp)
                            *tmp = '\0'; // tolgo il nome file dal path, mi resta il path per la dir
                        
                        // Creo la cartella ( RWX for owner )
                        if ( mkdir(pathcpy, S_IRWXU) != 0 ){
                            if ( errno == EEXIST ){
                                // Faccio a mano
                                char* c;
                                char *newtmp = malloc((strlen(tmp)+1)*sizeof(char));
                                strcpy(newtmp, tmp);
                                for ( c = newtmp; *c; c++ ){
                                    if ( *c == '/' ){
                                        *c = '\0';
                                        mkdir(newtmp, S_IRWXU);
                                        *c = '/';
                                    }
                                }
                                free(newtmp);
                            }
                            else{
                                if ( p ) fprintf(stderr, "CLIENT %d: Errore nella creazione della directory dove salvare i file letti - %s\n", pid, strerror(errno));                        
                                free(path);
                                free(pathcpy);
                                continue;
                            }
                        }
                        mode_t oldmask = umask(033);
                        // Creo finalmente il file dove scrivere il contenuto letto in binario
                        FILE* file = fopen(path, "w+");
                        umask(oldmask);
                        if (buf) fputs(buf, file); // Il terminatore per i file binari è già stato aggiunto dall'API
                        fclose(file);
                        
                        free(path);
                        free(pathcpy);
                    }
                    
                    memmove(token, temp, strlen(temp)+1);
                    strtok_r(token, ",", &temp);
                    free(buf);
                } while ( strcmp(token, "") != 0 );

                free(size);
                free(token);
                if ( dir_letti) myFree(dir_letti);
                lastOp = 'r';
            } break;
            case 'R':{
                int n = -1;

                if ( optarg ){
                    char* token = strdup(optarg);
                    if ( token[0] != 'n' || token[1] != '=' ){
                        if (p) fprintf(stderr, "CLIENT %d: Parametri errati nella chiamata ReadNFiles (token[0] - %c token[1] - %c)\n", pid, token[0], token[1]);
                        free(token);
                        continue;
                    }
                    n = strtol(token+2, NULL, 10);
                    if ( errno == ERANGE || errno == EINVAL ){
                        if (p) fprintf(stderr, "CLIENT %d: Parametri errati nella chiamata ReadNFiles)\n", pid);
                        free(token);
                        continue;
                    }
                    free(token);
                }

                // Controllo se è stata indicata la cartella dove salvare i file letti
                if ( count+2 < argc )
                    if ( argv[count+1][0] == '-' && argv[count+1][1] == 'd' ){
                        dir_letti = malloc(strlen(argv[count+2])+1);
                        strncpy(dir_letti, argv[count+2], strlen(argv[count+2])+1);
                    }

                if ( dir_letti ) printf("CLIENT %d: La cartella per i file letti è %s\n", pid, dir_letti);
                else printf("CLIENT %d: Non è stata speficificata la cartella dove salvare i file letti\n", pid);

                errno = 0;
                int esito = readNFiles(n, dir_letti);
                int err = errno;
                if ( p ){
                    if ( esito == 0 ) printf("CLIENT %d: Eseguito con successo readNFiles(%d, %s)\n", pid, n, dir_letti);
                    else printf("CLIENT %d: ReadNFiles fallita - %s\n", pid, strerror(err));
                }

                myFree(dir_letti);
                lastOp = 'R';
            } break;
            case 'w':{
                if ( !optarg ){
                    if (p) fprintf(stderr, "CLIENT %d: Parametri errati nella chiamata writeFile\n", pid);
                    continue;
                }

                char* dirname = strdup(optarg);
                int n = -1;
                char* token = strrchr(dirname, ',');
                if (token) {
                    n = strtol(token+2, NULL, 10);
                    if ( errno == ERANGE || errno == EINVAL ){
                        if (p) fprintf(stderr, "CLIENT %d: Parametri errati nella chiamata writeFile\n", pid);
                        free(dirname);
                        continue;
                    }
                    strtok(dirname, ",");
                }

                if ( count+2 < argc )
                    if ( argv[count+1][0] == '-' && argv[count+1][1] == 'd' ){
                        dir_espulsi = malloc(strlen(argv[count+2])+1);
                        strncpy(dir_espulsi, argv[count+2], strlen(argv[count+2])+1);
                    }

                // Inizio a visitare ricorsivamente dirname
                Queue_t *q = initQueue();
                
                int nfile = fileToWrite(n, dirname, q);
                for ( int i = 0; i < nfile; i++ ){
                    char* filename = pop(q);
                    errno = 0;
                    int esito = writeFile(filename, dir_espulsi);
                    int err = errno;
                    if ( p ){
                        if ( esito == 0 ) printf("CLIENT %d: eseguita con successo writeFile(%s)\n", pid, filename);
                        else printf("CLIENT %d: writeFile fallita - %s\n", pid, strerror(err));
                    }
                }

                free(dirname);
                if ( dir_espulsi ) myFree(dir_espulsi);
                lastOp = 'w';
            } break;
            case 'W':{
                if ( !optarg ){
                    if ( p ) printf("CLIENT %d: Parametri errati nella chiamata writeFile\n", pid);
                    continue;
                }
                char* token = strdup(optarg), *temp = NULL;
                
                // Controllo se è stata indicata la cartella dove salvare i file letti
                if ( count+2 < argc )
                    if ( argv[count+1][0] == '-' && argv[count+1][1] == 'd' ){
                        dir_espulsi = malloc(strlen(argv[count+2])+1);
                        strncpy(dir_espulsi, argv[count+2], strlen(argv[count+2])+1);
                    }

                if ( dir_espulsi ) printf("CLIENT %d: La cartella per i file letti è %s\n", pid, dir_espulsi);
                else printf("CLIENT %d: Non è stata speficificata la cartella dove salvare i file letti\n", pid);
                strtok_r(token, ",", &temp);

                do{                    
                    errno = 0;
                    int esito = writeFile(token, dir_espulsi);
                    int err = errno;
                    if ( p ){
                        if ( esito == 0 ) printf("CLIENT %d: eseguita con successo writeFile(%s)\n", pid, token);
                        else printf("CLIENT %d: writeFile fallita - %s\n", pid, strerror(err));
                    }

                    memmove(token, temp, strlen(temp)+1);
                    strtok_r(token, ",", &temp);
                    
                } while ( strcmp(token, "") != 0 );

                free(token);
                if ( dir_espulsi) myFree(dir_espulsi);
                lastOp = 'W';
            } break;
            case 'l':{
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
                if ( !optarg ){
                    if ( p ) printf("CLIENT %d: Parametri errati nella chiamata unlockFile\n", pid);
                    continue;
                }
                char* token = strdup(optarg), *temp = NULL;
                
                strtok_r(token, ",", &temp);

                do{                    
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
                if ( !optarg ){
                    if ( p ) printf("CLIENT %d: Parametri errati nella chiamata closeFile\n", pid);
                    continue;
                }
                char* token = strdup(optarg), *temp = NULL;
                
                strtok_r(token, ",", &temp);

                do{                    
                    errno = 0;
                    int esito = lockFile(token);
                    int err = errno;
                    if ( p ){
                        if ( esito == 0 ) printf("CLIENT %d: eseguita con successo closeFile(%s)\n", pid, token);
                        else printf("CLIENT %d: closeFile fallita - %s\n", pid, strerror(err));
                    }

                    memmove(token, temp, strlen(temp)+1);
                    strtok_r(token, ",", &temp);
                    
                } while ( strcmp(token, "") != 0 );

                free(token);
                lastOp = 'c';
            } break;
            case 'd':{
                if ( lastOp == 'r' || lastOp == 'R' )
                    continue;
                
                fprintf(stderr, "CLIENT %d: È stata specificata la cartella dove salvare i file in lettura, ma non è stata richiesta nessuna lettura\n", pid);
            }   break;
            case 'D':{
                if ( lastOp == 'W' || lastOp == 'w' )
                    continue;

                fprintf(stderr, "CLIENT %d: È stata specificata la cartella dove salvare i file espulsi dal server, ma non è stata richiesta nessuna scrittura\n", pid);
            }
        }
    }

    free(programname);
    return 0;
}
