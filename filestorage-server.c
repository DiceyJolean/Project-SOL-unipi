
#include "server-utility.h"


int main(int argc, char* argv[]) {

    // Setto i parametri dal file di configurazione
    if ( set_config(argc, argv) != 0 )
        return 0;

    // Installo il signal handler per i segnali da gestire
    installSigHand();

    // Apro la connessione socket
    int listenfd = openServerConnection(); // Socket su cui fare la accept

    // Il server accetta nuove connessioni e smista le richeste dei client fino all'arrivo di un segnale di chiusura
    while ( !sighup || ! sigint || ! sigquit ){
        int fdmax, tmpfdmax;
        fd_set readset, tmpset;
        // azzero sia il master set che il set temporaneo usato per la select
        FD_ZERO(&readset);
        FD_ZERO(&tmpset);

        // aggiungo il listener fd al master set
        FD_SET(listenfd, &readset);
        // TODO Aggiungere al readset la pipe per reinserire il fd servito dai worker e la pipe per la terminazione

    }
    if ( sighup ){
        // Uscita gentile, chiudo il socket per le nuove connessioni e attendo che tutti i client si disconnettano


    }
    else{
        // Uscita immediata


    }

    return 0;
}
