./filestorage -f config2.txt &
PID=$!

# Scrivo 10 file
./client -p -t 200 -f filesocket -w ./test/Inferno1-10/ -D ./espulsi/ &
# Scrivo altri tre file che causeranno l'espulsione dei primi tre canti
./client -p -t 200 -f filesocket -W ./test/long1.txt,./test/long2.txt,./test/long3.txt -D ./espulsi/ &&
# Leggo tutti i file presenti e li salvo per verificare il funzionamento dell'algoritmo di rimpiazzo
./client -p -t 200 -f filesocket -R n=-1 -d ./letti/

sleep 0.3 &

kill -s HUP $PID
wait $PID

cat stat.log

exit 0
