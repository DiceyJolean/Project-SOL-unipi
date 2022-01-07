// Erica Pistolesi 518169

#ifndef UTIL_H
#define UTIL_H

#ifndef EFATAL
#define EFATAL -1
#endif

#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

// Tipo di operazione richiesta
typedef enum {
	OPEN_FILE = 1,		// Apre o crea un file, controllo sui flags 
	CLOSE_FILE = 2,		// Chiude il file indicato
	WRITE_FILE = 3,		// Scrive su un file APPENA creato
	READ_FILE = 4,		// Legge il file indicato dalla tabella hash
	APPEND_FILE = 5,	// Scrive in append su il file indicato
	READ_N_FILES = 6,	// Legge al più N files dalla tabella hash (o tutti se N<=0)
	
	CLOSE_CONNECTION = 8,        // Chiude la connessione
	LOCK_FILE = 9,
	UNLOCK_FILE = 10,
	REMOVE_FILE = 11
} op_t;

// Struttura del messaggio per inviare richieste al server
typedef struct request{
    int fd;
    op_t op;
	int flags_N;

} request_t;

/**
 * 	@brief Funzione che permette di fare la write in modo che, se è interrotta da un segnale, riprende
 *
 * 	@param fd     descrittore della connessione
 * 	@param msg    puntatore al messaggio da inviare
 *
 * 	@return Il numero di bytes scritti, -1 se c'è stato un errore
 */
int writen(long fd, const void *buf, size_t nbyte){
	int writen = 0, w = 0;

	while ( writen < nbyte ){
		if ( (w = write(fd, buf, nbyte) ) == -1 ){
			/* se la write è stata interrotta da un segnale riprende */
			if ( errno == EINTR )
				continue;
			else if ( errno == EPIPE )
				break;
			else
				return -1;
		}
		if( w == 0 )
			return writen;
		
		writen += w;;
	}
	
	return writen;
}

/**
 * 	@brief Funzione che permette di fare la read in modo che, se è interrotta da un segnale, riprende
 *
 * 	@param fd     descrittore della connessione
 * 	@param msg    puntatore al messaggio da inviare
 *
 * 	@return Il numero di bytes letti, -1 se c'e' stato un errore
 */		
int readn(long fd, void *buf, size_t size) {
	int readn = 0, r = 0;
	
	while ( readn < size ){
		
		if ( (r = read(fd, buf, size)) == -1 ){
			if( errno == EINTR )
				// se la read è stata interrotta da un segnale riprende
				continue;
			else
				return -1;
		}
		if ( r == 0 )
			return readn; // Nessun byte da leggere rimasto

		readn += r;
	}
	
	return readn;
}

#endif //UTIL_H
