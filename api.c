// Erica Pistolesi 518169

#ifndef API_H_
#define API_H_

#if !defined(BILLION)
#define BILLION 1000000000
#endif

#if !defined(UNIX_PATH_MAX)
#define UNIX_PATH_MAX  64
#endif

#if !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE  600
#endif

#ifndef DEBUG
#define DEBUG 0
#endif

#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>
#include <sys/un.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

#include "api.h"
#include "util.h"

// File descriptor associato al client
static int fdc;
// Nome della socket con cui sono connesso
static char* socketname = NULL;

char* my_strcpy(char* dest){
    int len = (strlen(dest)+1)*sizeof(char);

    char* src = malloc(len);
    memset(src, 0, len);
    strncpy(src, dest, len);

    if ( !src )
        return NULL;

    return src;
}

// Se non esiste, crea l'albero di directory dirname
int my_mkdirP(char* dirname){
    for (char* p = strchr(dirname + 1, '/'); p; p = strchr(p + 1, '/')) {
        *p = '\0';
        if (mkdir(dirname, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == -1) {
            if (errno != EEXIST) {
                *p = '/';
                printf("my_mkdirP: Errore nella creazione di %s - %s\n", dirname, strerror(errno));
                return -1;
            }
        }
        *p = '/';
    }
    if ( DEBUG ) printf("my_mkdirP: Ho creato la directory %s, il path assoluto è %s\n", dirname, realpath(dirname, NULL));

    return 0;
}

// Crea e salva un file di nome filename, con contenuto content nella directory corrente (non è in api.h, non crea conflitto con la funzione del client)
int saveFile(char* filename, void* content){
	if ( !filename )
		return -1;

	// Nome "pulito" del file, senza il percorso completo
	char* filecpy = my_strcpy(filename);
	char* tmp = strrchr(filecpy, '/');
	tmp++;
	if ( DEBUG ) printf("api: Il nome del file senza il path assoluto è %s\n", tmp);

	mode_t oldmask = umask(033);
	FILE* file = fopen(tmp, "w+");

	free(filecpy);
	umask(oldmask);

	if ( !file )
		return -1;

	if ( content ){
		fputs(content, file);
		fputs("\n", file);
	}

	fclose(file);

	return 0;
}

request_t* prepare_request(op_t op, int flag){
	
	request_t* req = malloc(sizeof(request_t));
	if ( !req ){
		errno = ENOMEM;
		return NULL;
	}
	memset(req, 0, sizeof(request_t));
	req->fd = fdc;
	req->op = op;
	req->flags_N = flag;

	return req;
}

// Invia il pathname tramite socket, se il pathname non corrisponde a un file su disco fallisce con EINVAL
int send_pathname(const char* pathname){

	char* abspath = realpath(pathname, NULL);
	if ( !abspath ){
		printf("API: Errore nel generare il path assoluto di %s\n", pathname);
		errno = EINVAL;
		return -1;
	}

	int n = strlen(abspath)+1;

	if ( write(fdc, &n, sizeof(int)) == -1){
		errno = EPIPE;
		return -1;
	}
	if ( writen(fdc, abspath, n) == -1){
		errno = EPIPE;
		return -1;
	}

	return 0;
}

int read_answer(){
	int esito = -1; // Risposta da parte del server (0 o -1)
	
	errno = 0;
	if ( read(fdc, &esito, sizeof(int)) == -1 || esito == -1 ){
		errno = EPIPE;
		return -1;
	}

	return esito;
}

// Return the number of seconds between before and after, (after - before).
double timespec_diff(const struct timespec after, const struct timespec before){
    return (double)(after.tv_sec - before.tv_sec) + (double)(after.tv_nsec - before.tv_nsec) / BILLION;
}

int openConnection(const char* sockname, int msec, const struct timespec abstime){

	int connected = 0;
	struct sockaddr_un client_addr;
    errno = 0;
	
	client_addr.sun_family = AF_UNIX;
	strncpy(client_addr.sun_path, sockname, UNIX_PATH_MAX);
	fdc = socket(AF_UNIX, SOCK_STREAM, 0);
	
	if ( fdc == -1 )
		return -1;

    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
	while ( timespec_diff(abstime, now) > 0 && !connected ){
		errno = 0;
		if ( ( connect(fdc,(struct sockaddr *)&client_addr, sizeof(client_addr)) ) == -1 ) {
			if ( errno == ENOENT || errno == ECONNREFUSED )
				sleep( msec );
			else
				return -1;
		}
		else 
			connected = 1 ;

        clock_gettime(CLOCK_REALTIME, &now);
	}
	
	if ( !connected )
		return -1;

	socketname = malloc((strlen(sockname)+1)*sizeof(char));
	strcpy(socketname, sockname);
	
	return 0;
}

int closeConnection(const char* sockname){
	
	if ( fdc == -1 ){
		errno = EPERM;
		return -1;
	}

	if ( strncmp(socketname, sockname, UNIX_PATH_MAX) != 0 ){
		errno = EINVAL;
		return -1;
	}

	request_t* req = prepare_request(CLOSE_CONNECTION, 0);
	if ( !req )
		return -1;

	if ( writen(fdc, req, sizeof(request_t)) == -1 ){
		errno = EPIPE;
		return -1;
	}

    if ( close(fdc) == -1 )
        return -1; // errno già settato dalla close

	free(socketname);
	free(req);
	return 0;
}

int openFile(const char* pathname, int flags){

	request_t* req = prepare_request(OPEN_FILE, flags);
	if ( !req )
		return -1;

	if ( writen(fdc, req, sizeof(request_t) ) == -1 ){
		errno = EPIPE;
		return -1;
	}

	if ( send_pathname(pathname) )
		return -1;

	int reply;
	if ( ( reply = read_answer()) ){
		free(req);
		errno = reply;
		return -1;
	}

	free(req);

	return 0;
}

int readFile(const char* pathname, void** buf, size_t* size){

	request_t* req = prepare_request(READ_FILE, 0);
	if ( !req )
		return -1;

	if ( !size )
		size = malloc(sizeof(size_t));

	*size = 0;
	if ( writen(fdc, req, sizeof(request_t)) == -1 ){
		errno = EPIPE;
		return -1;
	}

	if ( send_pathname(pathname) )
		return -1;

	int reply; // Capisco se l'operazione è andata a buon fine
	if ( ( reply = read_answer()) != 0 ){
		free(req);
		errno = reply;
		return -1;
	}

	size_t n = 0; // Numero di bytes del file da salvare in buf
	errno = 0;
	if ( read(fdc, &n, sizeof(size_t)) == -1 ){
		errno = EPIPE;
		return -1;
	}

	*buf = NULL;
	char* tmp = malloc(n+1);
	if ( tmp ){
		memset(tmp, 0, n+1);
		
		if ( readn(fdc, tmp, n) == -1 ){
			errno = EPIPE;
			return -1;
		}
		tmp[n] = '\0';
		*buf = tmp;
		*size = n;
	}
	else{
		errno = ENOMEM;
		return -1;
	}

	free(req);
	return 0;
}

int readNFiles(int N, const char* dirname){

	request_t* req = prepare_request(READ_N_FILES, N);
	if ( !req )
		return -1;

	if ( writen(fdc, req, sizeof(request_t)) == -1 ){
		errno = EPIPE;
		return -1;
	}

	int reply; // Capisco se l'operazione è andata a buon fine
	if ( ( reply = read_answer()) != 0 ){
		free(req);
		errno = reply;
		return -1;
	}
	
	// Leggo quanti file ricevo
	int res = -1;
	if ( read(fdc, &res, sizeof(int)) == -1 ){
		errno = EPIPE;
		return -1;
	}

	// Salvo la directory corrente
	char* currentdir = malloc(MAX_PATHNAME*sizeof(char));
	if ( dirname ){
		// Se è stata indicata la cartella dove salvare i file la creo e mi ci sposto dentro
		if ( DEBUG ) printf("api: Creo la directory %s\n", dirname);
		// Recupero il percorso della directory attuale
		getcwd(currentdir, MAX_PATHNAME);
		if ( !currentdir)
			return -1;
		
		// creo la cartella dove salvare i file
		
		char* dircpy = my_strcpy((char*)dirname);
		if ( my_mkdirP(dircpy) == 0 ){
			if ( DEBUG ) printf("api: Mi sposto nella cartella %s\n", dircpy);
			// Mi sposto nella cartella dove salvare i file
			chdir(dircpy);
		}
		else if ( DEBUG ) printf("api: Creazione della directory %s fallita\n", dirname);

		free(dircpy);
	}

	if ( DEBUG ) printf("api: Mi preparo a salvare %d file nella cartella %s\n", res, dirname);
	for ( int i = 0; i < res; i++ ){
		// leggo il filename
		size_t n = -1;

		if ( readn(fdc, &n, sizeof(size_t)) == -1){
			errno = EPIPE;
			return -1;
		}

		char* filename = malloc(n);
		if ( readn(fdc, filename, n) == -1){
			errno = EPIPE;
			return -1;
		}

		// leggo il contenuto
		if ( readn(fdc, &n, sizeof(size_t)) == -1){
			errno = EPIPE;
			return -1;
		}

		void* buf = malloc(n +1);
		memset(buf, 0, n +1 );
		if ( readn(fdc, buf, n) == -1){
			errno = EPIPE;
			return -1;
		}

		if ( dirname ){
			// Salvo il file
			if ( saveFile(filename, buf) != 0 ){
				if ( DEBUG ) printf("api: Errore nella creazione del file %s\n", filename);
				else if ( DEBUG ) printf("api: Creazione del file %s eseguita con successo\n", filename);
			}
		}

		free(filename);
		free(buf);
		buf = NULL;
	}

	if ( dirname ) chdir(currentdir);
	free(currentdir);
	free(req);

	return 0;
}

int writeFile(const char* pathname, const char* dirname){

	int fdfile = open(pathname, O_RDONLY);
	if ( fdfile == -1 )
		return -1; // errno già settato dalla open

	struct stat info;
	if ( stat(pathname, &info) == -1 )
		return -1; // errno già settato dalla stat
	
	size_t size = info.st_size+1;
	void* buf = malloc(size);
	memset(buf, 0, size);
	readn(fdfile, buf, size);
	close(fdfile);
   
	request_t* req = prepare_request(WRITE_FILE, 0);
	if ( !req )
		return -1;
	
	if ( writen(fdc, req, sizeof(request_t)) == -1 ){
		errno = EPIPE;
		return -1;
	}

	if ( send_pathname(pathname) )
		return -1;

	// Comunico quanti bytes voglio scrivere
	if ( DEBUG )	printf("\nAPI: Sto per inviare %ld bytes\n\n", size);
	if ( writen(fdc, &size, sizeof(size_t)) == -1 ){
		errno = EPIPE;
		return -1;
	}

	if ( DEBUG )	printf("\nAPI: Sto per inviare il contenuto \"%s\"\n\n", (char*)buf);
	// Invio il contenuto da scrivere
	if ( writen(fdc, buf, size) == -1 ){
		errno = EPIPE;
		return -1;
	}

	int reply;
	if ( DEBUG ) printf("\nAPI: Attendo la risposta dal server\n\n");
	
	reply = read_answer();
	if ( DEBUG ) printf("\nAPI: Il server ha risposto - %s\n\n", strerror(reply));

	int len = 0; // numero di file espulsi
	if ( read(fdc, &len, sizeof(int)) == -1 ){
		errno = EPIPE;
		return -1;
	}

	// Salvo la directory corrente
	char* currentdir = malloc(MAX_PATHNAME*sizeof(char));
	if ( dirname ){
		// Se è stata indicata la cartella dove salvare i file la creo e mi ci sposto dentro
		if ( DEBUG ) printf("API: Creo la directory %s\n", dirname);
		// Recupero il percorso della directory attuale
		getcwd(currentdir, MAX_PATHNAME);
		if ( !currentdir)
			return -1;
		
		// creo la cartella dove salvare i file
		
		char* dircpy = my_strcpy((char*)dirname);
		if ( my_mkdirP(dircpy) == 0 ){
			if ( DEBUG ) printf("API: Mi sposto nella cartella %s\n", dircpy);
			// Mi sposto nella cartella dove salvare i file
			chdir(dircpy);
		}
		else if ( DEBUG ) printf("API: Creazione della directory %s fallita\n", dirname);

		free(dircpy);
	}

	if ( DEBUG ) printf("API: Mi preparo a salvare %d file nella cartella %s\n", len, dirname);
	// Sto per ricevere n files
	for ( int i = 0; i < len; i++ ){
		// leggo il pathname
		size_t n = -1;

		if ( readn(fdc, &n, sizeof(size_t)) == -1){
			errno = EPIPE;
			return -1;
		}

		char* filename = malloc(n);
		if ( readn(fdc, filename, n) == -1){
			errno = EPIPE;
			return -1;
		}

		// leggo il contenuto
		if ( readn(fdc, &n, sizeof(size_t)) == -1){
			errno = EPIPE;
			return -1;
		}

		void* buf = malloc(n);
		memset(buf, 0, n);
		if ( readn(fdc, buf, n) == -1){
			errno = EPIPE;
			return -1;
		}

		if ( dirname ){
			// Salvo il file
			if ( saveFile(filename, buf) != 0 ){
				if ( DEBUG ) printf("api: Errore nella creazione del file %s\n", filename);
				else if ( DEBUG ) printf("api: Creazione del %s eseguita con successo\n", filename);
			}
		}

		free(filename);
		free(buf);
		buf = NULL;
	}
	
	free(req);
	if ( dirname ) chdir(currentdir);
	free(currentdir);

	if ( reply ){
		errno = reply;
		return -1;
	}

	return 0;
}

int appendToFile(const char* pathname, void* buf, size_t size, const char* dirname){

	request_t* req = prepare_request(APPEND_FILE, 0);
	if ( !req )
		return -1;

	if ( writen(fdc, req, sizeof(request_t)) == -1 ){
		errno = EPIPE;
		return -1;
	}

	if ( send_pathname(pathname) )
		return -1;

	if ( !buf )
		buf = malloc(size);

	// Comunico quanti bytes voglio scrivere
	if ( DEBUG ) printf("API: Comunico al server che voglio scrivere %ld bytes in append\n", size);
	if ( writen(fdc, &size, sizeof(size_t)) == -1 ){
		errno = EPIPE;
		return -1;
	}

	// Invio il contenuto da scrivere in append
	if ( DEBUG ) printf("API: Invio al server il contenuto da scrivere in append\n");
	if ( writen(fdc, buf, size) == -1 ){
		errno = EPIPE;
		return -1;
	}

	// Mi aspetto l'esito della scrittura come risposta
	int reply;
	if ( DEBUG ) printf("\nAPI: Attendo la risposta dal server\n\n");
	
	reply = read_answer();
	if ( DEBUG ) printf("\nAPI: Il server ha risposto - %s\n\n", strerror(reply));

	int n = 0; // numero di file espulsi
	if ( read(fdc, &n, sizeof(int)) == -1 ){
		errno = EPIPE;
		return -1;
	}
	
	// Sto per ricevere n files
	for ( int i = 0; i < n; i++ ){
		// leggo il pathname
		size_t n = -1;

		if ( readn(fdc, &n, sizeof(size_t)) == -1){
			errno = EPIPE;
			return -1;
		}

		char* file = malloc(n);
		if ( readn(fdc, file, n) == -1){
			errno = EPIPE;
			return -1;
		}

		// leggo il contenuto
		if ( readn(fdc, &n, sizeof(size_t)) == -1){
			errno = EPIPE;
			return -1;
		}

		void* buf = malloc(n +1);
		memset(buf, 0, n +1 );
		if ( readn(fdc, buf, n) == -1){
			errno = EPIPE;
			return -1;
		}

		if ( dirname ){
			char* absdir = realpath(dirname, NULL);
			char* path = (char*)malloc((strlen(absdir)+1 + strlen(file))*sizeof(char));
			memset(path, 0, (strlen(absdir)+1 + strlen(file))*sizeof(char));
			// Creo il path della directory dove andrà salvato
			strcpy(path, absdir);
			// E poi ci concateno il path assoluto del file
			strcat(path, file);

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
					free(path);
					free(pathcpy);
					errno = EFATAL;
					return -1;
				}
			}
			mode_t oldmask = umask(033);
			// Creo finalmente il file dove scrivere il contenuto letto in binario
			FILE* file = fopen(path, "w+");
			umask(oldmask);
			if (buf){
				fputs(buf, file);
				fputs("\n", file);
			}
			fclose(file);
			
			free(path);
			free(pathcpy);
		}

		free(buf);
		buf = NULL;
	}

	return 0;
}

int closeFile(const char* pathname){

	request_t* req = prepare_request(CLOSE_FILE, 0);
	if ( !req )
		return -1;
		
	if ( writen(fdc, req, sizeof(request_t)) == -1 ){
		errno = EPIPE;
		return -1;
	}

	if ( send_pathname(pathname) )
		return -1;

	int reply; // Capisco se l'operazione è andata a buon fine
	if ( ( reply = read_answer()) ){
		free(req);
		errno = reply;
		return -1;
	}

	free(req);

	return 0;
}

int lockFile(const char* pathname){

	request_t* req = prepare_request(LOCK_FILE, 0);
	if ( !req )
		return -1;

	if ( writen(fdc, req, sizeof(request_t)) == -1 ){
		errno = EPIPE;
		return -1;
	}

	if ( send_pathname(pathname) )
		return -1;

	int reply; // Capisco se l'operazione è andata a buon fine
	// Rimarrò in attesa su questa read finché non ottengo la lock oppure non ricevo un errore
	if ( ( reply = read_answer()) ){
		free(req);
		errno = reply;
		return -1;
	}

	free(req);

    return  0;
}

int unlockFile(const char* pathname){

	request_t* req = prepare_request(UNLOCK_FILE, 0);
	if ( !req )
		return -1;

	if ( writen(fdc, req, sizeof(request_t)) == -1 ){
		errno = EPIPE;
		return -1;
	}

	if ( send_pathname(pathname) )
		return -1;
	
	int reply; // Capisco se l'operazione è andata a buon fine
	if ( DEBUG ) printf("API: Attendo la risposta dal Server\n");
	if ( ( reply = read_answer()) ){
		free(req);
		errno = reply;
		return -1;
	}

	free(req);

    return  0;
}

int removeFile(const char* pathname){

	request_t* req = prepare_request(REMOVE_FILE, 0);

	errno = 0;
	if ( writen(fdc, req, sizeof(request_t)) == -1 ){
		errno = EPIPE;
		return -1;
	}

	if ( send_pathname(pathname) )
		return -1;

	int reply; // Capisco se l'operazione è andata a buon fine
	if ( ( reply = read_answer()) ){
		free(req);
		errno = reply;
		return -1;
	}

	free(req);

    return  0;
}

#endif //API_H_
