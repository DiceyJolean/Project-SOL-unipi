
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

static int fdc = -1;
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
	if ( DEBUG ) printf("api: Il nome del file senza il path assoluto è %s\n", ++tmp);

	mode_t oldmask = umask(033);
	FILE* file = fopen(++tmp, "w+");

	free(filecpy);
	umask(oldmask);

	if ( !file )
		return -1;

	if ( content )
		fputs(content, file);

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

int send_pathname(const char* pathname){

	char* abspath = realpath(pathname, NULL);
	if ( !abspath ){
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

/** @brief  Viene aperta una connessione AF_UNIX al socket file sockname.
 *          Se il server non accetta immediatamente la richiesta di connessione,
 *          la connessione da parte del client viene ripetuta dopo ‘msec’ millisecondi
 *          e fino allo scadere del tempo assoluto ‘abstime’ specificato come terzo argomento.
 *          errno viene settato opportunamente.
 * 
 *  @param sockname Nome del file socket
 *  @param msec  Millisecondi dopo quanto viene eseguito un nuovo tentativo di connessione se il precedente fallisce
 *  @param abstime   Tempo assoluto di attesa per connettersi
 * 
 *  @return 0 in caso di successo; -1 in caso di fallimento
 */
int openConnection(const char* sockname, int msec, const struct timespec abstime){

	int connected = 0;
	struct sockaddr_un client_addr;
    errno = 0;
	
	client_addr.sun_family = AF_UNIX;
	strncpy(client_addr.sun_path, sockname, UNIX_PATH_MAX);
    	
	if ( ( fdc = socket(AF_UNIX, SOCK_STREAM, 0) ) == -1 ){
		perror("Socket");
		return -1;
	}

    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
	while ( timespec_diff(abstime, now) > 0 && !connected ){
		errno = 0;
		if ( ( connect(fdc,(struct sockaddr *)&client_addr, sizeof(client_addr)) ) == -1 ) {
			if ( errno == ENOENT || errno == ECONNREFUSED )
				sleep( msec );
			else{
				perror("Connect");
				return -1;
			}
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

/** @brief  Chiude la connessione AF_UNIX associata al socket file sockname.
 *          errno viene settato opportunamente.
 *          
 *  @param sockname Nome del file socket
 * 
 *  @return 0 in caso di successo; -1 in caso di fallimento
 */
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

/** @brief  Richiesta di apertura o di creazione di un file.
 *          La semantica della openFile dipende dai flags passati come
 *          secondo argomento che possono essere O_CREATE ed O_LOCK.
 *          Se viene passato il flag O_CREATE ed il file esiste già memorizzato nel server,
 *          oppure il file non esiste ed il flag O_CREATE non è stato specificato,
 *          viene ritornato un errore. In caso di successo, il file viene sempre aperto 
 *          in lettura e scrittura, ed in particolare le scritture possono avvenire solo in append.
 *          Se viene passato il flag O_LOCK (eventualmente in OR con O_CREATE) il file viene 
 *          aperto e/o creato in modalità locked, che vuol dire che l’unico che può leggere o 
 *          scrivere il file ‘pathname’ è il processo che lo ha aperto. Il flag O_LOCK può essere 
 *          esplicitamente resettato utilizzando la chiamata unlockFile, descritta di seguito. 
 *          errno viene settato opportunamente.
 * 
 *  @param pathname File da creare
 *  @param flags    O_CREATE e/o O_LOCK
 * 
 *  @return 0 in caso di successo; -1 in caso di fallimento
 */
int openFile(const char* pathname, int flags){

	request_t* req = prepare_request(OPEN_FILE, flags);

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

/** @brief  Legge tutto il contenuto del file dal server (se esiste) ritornando un puntatore 
 *          ad un'area allocata sullo heap nel parametro ‘buf’, mentre ‘size’ conterrà la dimensione del buffer
 *          dati (ossia la dimensione in bytes del file letto). In caso di errore, ‘buf‘ e ‘size’ non sono validi.
 *          errno viene settato opportunamente.
 * 
 *  @param pathname File da leggere
 *  @param buf      Area sullo heap dove vengono allocati i bytes letti (?)
 *  @param size     Dimensione aggiornata del buffer
 * 
 *  @return 0 in caso di successo; -1 in caso di fallimento
 */
int readFile(const char* pathname, void** buf, size_t* size){

	request_t* req = prepare_request(READ_FILE, 0);
	if ( !req )
		return -1;

	if ( !size )
		size = malloc(sizeof(size_t));

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

	size_t n = -1; // Numero di bytes del file da salvare in buf
	errno = 0;
	if ( read(fdc, &n, sizeof(size_t)) == -1 || n == -1 ){
		errno = EPIPE;
		return -1;
	}

	*buf = NULL;
	if ( n > 0 ){
		// Devo conoscere la dimensione del file passato, in esito ci sarà il numero di bytes (n delle esercitazioni)
		char* tmp = malloc(n+1);
		memset(tmp, 0, n+1);
		
		if ( readn(fdc, tmp, n) == -1 ){
			errno = EPIPE;
			return -1;
		}
		tmp[n] = '\0';
		*buf = tmp;
	}
	*size = n;

	free(req);
	return 0;
}

/**	@brief 	Richiede al server la lettura di ‘N’ files qualsiasi da memorizzare nella directory ‘dirname’ lato client.
 * 			Se il server ha meno di ‘N’ file disponibili, li invia tutti. Se N<=0 la richiesta al server è quella di
 * 			leggere tutti i file memorizzati al suo interno. Ritorna un valore maggiore o uguale a 0 in caso di successo
 * 
 * 	@param	N Numero di file da leggere
 * 	@param	dirname Se diverso da NULL, cartella dove scrivere i file letti
*/
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
				else if ( DEBUG ) printf("api: Creazione del %s eseguita con successo\n", filename);
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

/** @brief  Scrive tutto il file puntato da pathname nel file server. Ritorna successo solo se la precedente operazione,
 *          terminata con successo, è stata openFile(pathname,O_CREATE|O_LOCK). Se ‘dirname’ è diverso da NULL,
 *          il file eventualmente spedito dal server perchè espulso dalla cache per far posto al file ‘pathname’ dovrà
 *          essere scritto in ‘dirname’.
 *          errno viene settato opportunamente.
 * 
 *  @param pathname File da copiare nel file server
 *  @param dirname  Cartella dove scrivere l'eventuale file espulso dal server
 *
 *  @return 0 in caso di successo; -1 in caso di fallimento
 */
int writeFile(const char* pathname, const char* dirname){

	int fdfile = open(pathname, O_RDONLY);
	if ( fdfile == -1 )
		return -1; // errno già settato dalla open

	struct stat info;
	if ( stat(pathname, &info) == -1 )
		return -1; // errno già settato dalla stat
	
	size_t size = info.st_size+1;
	char* buf = malloc(size);
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

	// Aggiungo il terminatore \0
	if ( buf[size-1] != '\0' ){
		if ( DEBUG ) printf("API: Aggiungo il terminatore al file %s\n", pathname);
		size++;
		buf = realloc(buf, size);
		buf[size-1] = '\0';
	}

	// Comunico quanti bytes voglio scrivere
	if ( DEBUG )	printf("\nAPI: Sto per inviare %ld bytes\n\n", size);
	if ( writen(fdc, &size, sizeof(size_t)) == -1 ){
		errno = EPIPE;
		return -1;
	}

	if ( DEBUG )	printf("\nAPI: Sto per inviare il contenuto \"%s\"\n\n", buf);
	// Invio il contenuto da scrivere
	if ( writen(fdc, buf, size) == -1 ){
		errno = EPIPE;
		return -1;
	}

	int reply;
	if ( DEBUG ) printf("\nAPI: Attendo la risposta dal server\n\n");
	if ( ( reply = read_answer()) != 0 ){
		if ( DEBUG ) printf("\nAPI: Il server ha risposto - %s\n\n", strerror(reply));
		free(req);
		errno = reply;
		return -1;
	}
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

	return 0;
}

/** @brief  Richiesta di scrivere in append al file ‘pathname‘ i ‘size‘ bytes contenuti nel buffer ‘buf’.
 *          L’operazione di append nel file è garantita essere atomica dal file server. Se ‘dirname’ è diverso da NULL,
 *          il file eventualmente spedito dal server perchè espulso dalla cache per far posto ai nuovi dati di
 *          ‘pathname’ dovrà essere scritto in ‘dirname’.
 *          errno viene settato opportunamente.
 * 
 *  @param pathname File su cui scrivere in append
 *  @param buf      Contenuto da scrivere sul file
 *  @param size     Bytes da scrivere
 *  @param dirname  Cartella dove scrivere l'eventuale file espulso dal server
 * 
 *  @return 0 in caso di successo; -1 in caso di fallimento
 */
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

	// Comunico quanti bytes voglio scrivere
	if ( writen(fdc, &size, sizeof(size_t)) == -1 ){
		errno = EPIPE;
		return -1;
	}

	// Invio il contenuto da scrivere in append
	if ( writen(fdc, buf, size) == -1 ){
		errno = EPIPE;
		return -1;
	}

	// Mi aspetto l'esito della scrittura come risposta
	int reply;
	if ( ( reply = read_answer()) != 0 ){
		free(req);
		errno = reply;
		return -1;
	}

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
			if (buf) fputs(buf, file);
			fclose(file);
			
			free(path);
			free(pathcpy);
		}

		free(buf);
		buf = NULL;
	}

	return 0;
}

/** @brief  Richiesta di chiusura del file puntato da ‘pathname’.
 *          Eventuali operazioni sul file dopo la closeFile falliscono.
 *          errno viene settato opportunamente.
 * 
 *  @param pathname File da chiudere
 * 
 *  @return 0 in caso di successo; -1 in caso di fallimento
 */
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

/** @brief 	Richiesta di mutua esclusione sul file indicato da 'pathname'.
 * 			Se il file era già lockato da un altro client, chi fa la
 * 			richiesta viene sospeso fino a che chi detiene la lock non la rilascia.
 * 			errno viene settato opportunamente
 * 
 * 	@param pathname File da lockare
 * 
 * 	@return 0 in caso di successo, -1 in caso di fallimento
 * 
 */
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
	if ( ( reply = read_answer()) ){
		free(req);
		errno = reply;
		return -1;
	}

	free(req);

    return  0;
}

/**
 * 	@brief	Richiesta di rilasciare la mutua esclusione sul file
 * 			indicato da 'pathname',
 * 			errno viene settato opportunamente
 * 
 * 	@param pathname File da unlockare
 * 
 * 	@return 0 in caso di successo, -1 in caso di fallimento
 */
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
	if ( ( reply = read_answer()) ){
		free(req);
		errno = reply;
		return -1;
	}

	free(req);

    return  0;
}

/**
 * 	@brief	Richiesta di rimuovere dallo storage il file indicato da 'pathname'
 * 			errno viene settato opportunamente
 * 
 * 	@param pathname File da rimuovere
 * 
 * 	@return 0 in caso di successo, -1 in caso di fallimento
 */
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
