// Erica Pistolesi 518169

#ifndef API_H
#define API_H

#if !defined(MAX_PATHNAME)
#define MAX_PATHNAME 512
#endif

#ifndef OCREAT
#define OCREAT 1
#endif

#ifndef OLOCK
#define OLOCK 2
#endif

#include <unistd.h>
#include <sys/time.h>

// Esegue la copia di una stringa allocando la memoria
char* my_strcpy(char* dest);

// Se non esiste, crea l'albero di directory dirname
int my_mkdirP(char* dirname);

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
 *  @return 0 in caso di successo, -1 in caso di fallimento
 */
int openConnection(const char* sockname, int msec, const struct timespec abstime);

/** @brief  Chiude la connessione AF_UNIX associata al socket file sockname.
 *          errno viene settato opportunamente.
 *          
 *  @param sockname Nome del file socket
 * 
 * @exception	EPERM se non era stata stabilita nessuna connessione\n
 * 				EINVAL se il nome della socket non corrisponde\n
 * 				EPIPE se il server ha chiuso la comunicazione
 * 
 *  @return 0 in caso di successo, -1 in caso di fallimento
 */
int closeConnection(const char* sockname);

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
 * @exception EPIPE se il server ha chiuso la comunicazione\n
 *            ENOENT se il pathname non è presente in cache\n
 *            EINVAL se il pathname non corrisponde a un file su disco\n
 *            EEXIST se era stata indicato il flag OCREAT e il file era già presente in cache
 * 
 *  @return 0 in caso di successo, -1 in caso di fallimento
 */
int openFile(const char* pathname, int flags);

/** @brief  Legge tutto il contenuto del file dal server (se esiste) ritornando un puntatore 
 *          ad un'area allocata sullo heap nel parametro ‘buf’, mentre ‘size’ conterrà la dimensione del buffer
 *          dati (ossia la dimensione in bytes del file letto). In caso di errore, ‘buf‘ e ‘size’ non sono validi.
 *          errno viene settato opportunamente.
 * 
 *  @param pathname File da leggere
 *  @param buf      Area sullo heap dove vengono allocati i bytes letti (?)
 *  @param size     Dimensione aggiornata del buffer
 * 
 * @exception	EPIPE se il server ha chiuso la comunicazione\n
 *            ENOENT se il pathname non è presente in cache\n
 *            EINVAL se il pathname non corrisponde a un file su disco\n
 *            EPERM se il file è bloccato da un altro client, oppure se il client non ha aperto il file
 * 
 *  @return 0 in caso di successo, -1 in caso di fallimento
 */
int readFile(const char* pathname, void** buf, size_t* size);

/**	@brief 	Richiede al server la lettura di ‘N’ files qualsiasi da memorizzare nella directory ‘dirname’ lato client.
 * 			Se il server ha meno di ‘N’ file disponibili, li invia tutti. Se N<=0 la richiesta al server è quella di
 * 			leggere tutti i file memorizzati al suo interno. Ritorna un valore maggiore o uguale a 0 in caso di successo
 * 
 * 	@param	N Numero di file da leggere
 * 	@param	dirname Se diverso da NULL, cartella dove scrivere i file letti
 * 
 * 	@exception	EPIPE se il server ha chiuso la comunicazione
 * 
 *	@return 0 in caso di successo, -1 altrimenti
*/
int readNFiles(int N, const char* dirname);

/** @brief  Scrive tutto il file puntato da pathname nel file server. Ritorna successo solo se la precedente operazione,
 *          terminata con successo, è stata openFile(pathname,O_CREATE|O_LOCK). Se ‘dirname’ è diverso da NULL,
 *          il file eventualmente spedito dal server perchè espulso dalla cache per far posto al file ‘pathname’ dovrà
 *          essere scritto in ‘dirname’.
 *          errno viene settato opportunamente.
 * 
 *  @param pathname File da copiare nel file server
 *  @param dirname  Cartella dove scrivere l'eventuale file espulso dal server
 * 
 * 	@exception  EPIPE se il server ha chiuso la comunicazione\n
 * 				      ENOENT se il pathname non è presente in cache\n
 *              EINVAL se il pathname non corrisponde a un file su disco\n
 *              EPERM se il file è bloccato da un altro client, se l'operazione precedente non era una openFile(OCREAT), oppure se il client non ha aperto il file\n
 * 				      EFBIG se il file è troppo grande
 *
 *  @return 0 in caso di successo, -1 in caso di fallimento
 */
int writeFile(const char* pathname, const char*dirname);

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
 * 	@exception	EPIPE se il server ha chiuso la comunicazione\n
 * 				      ENOENT se il pathname non è presente in cache\n
 *              EINVAL se il pathname non corrisponde a un file su disco\n
 *              EPERM se il file è bloccato da un altro client, oppure se il client non ha aperto il file\n
 * 				      EFBIG se il contenuto da aggiungere è troppo grande
 * 
 *  @return 0 in caso di successo, -1 in caso di fallimento
 */
int appendToFile(const char* pathname, void* buf, size_t size, const char* dirname);

/** @brief  Richiesta di chiusura del file puntato da ‘pathname’.
 *          Eventuali operazioni sul file dopo la closeFile falliscono.
 *          errno viene settato opportunamente.
 *
 *  @param pathname File da chiudere
 * 
 *  @exception  EPIPE se il server ha chiuso la comunicazione\n
 *              EINVAL se il pathname non corrisponde a un file su disco\n
 *              ENOENT se il file non è presente in cache
 *
 *  @return 0 in caso di successo, -1 in caso di fallimento
 */
int closeFile(const char* pathname);

/**
 * @brief In caso di successo setta il flag O_LOCK al file. Se il file era stato aperto/creato con il flag O_LOCK e la
 *        richiesta proviene dallo stesso processo, oppure se il file non ha il flag O_LOCK settato, l’operazione
 *        termina immediatamente con successo, altrimenti l’operazione non viene completata fino a quando il flag
 *        O_LOCK non viene resettato dal detentore della lock. L’ordine di acquisizione della lock sul file non è
 *        specificato.
 *        errno viene settato opportunamente.
 *
 * @param pathname File da bloccare in mutua esclusione
 * 
 * @exception   EPIPE se il server ha chiuso la comunicazione\n
 *              EINVAL se il pathname non corrisponde a un file su disco\n
 *              ENOENT se il file non è presente in cache
 * 
 * @return 0 in caso di successo, -1 in caso di fallimento
 */
int lockFile(const char* pathname);

/**
 * @brief Resetta il flag O_LOCK sul file ‘pathname’. L’operazione ha successo solo se l’owner della lock è il processo
 * che ha richiesto l’operazione, altrimenti l’operazione termina con errore.
 * errno viene settato opportunamente.
 *
 * @param pathname File da sbloccare
 * 
 * @exception   EPIPE se il server ha chiuso la comunicazione\n
 *              ENOENT se il file non è presente in cache\n
 *              EINVAL se il pathname non corrisponde a un file su disco\n
 *              EPERM se il client non aveva la lock sul file
 * 
 * @return 0 in caso di successo, -1 in caso di fallimento
 */
int unlockFile(const char* pathname);

/**
 * @brief Rimuove il file cancellandolo dal file storage server. L’operazione fallisce se il file non è in stato locked,
 * o è in stato locked da parte di un processo client diverso da chi effettua la removeFile.
 * errno viene settato opportunamente.
 *
 * @param pathname File da rimuovere dal file storage
 * 
 * @exception   EPIPE se il server ha chiuso la comunicazione\n
 *              ENOENT se il file non è presente in cache\n
 *              EINVAL se il pathname non corrisponde a un file su disco\n
 *              EPERM se il client non aveva la lock sul file
 * 
 * @return 0 in caso di successo, -1 in caso di fallimento
 */
int removeFile(const char* pathname);

#endif //API_H
