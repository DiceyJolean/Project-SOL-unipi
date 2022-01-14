valgrind --leak-check=full ./filestorage -f config1.txt &
PID=$!
echo $PID

./client -p -t 200 -f filesocket -w ./test/Inferno11-20/,n=5 -D ./esplusi/ -R n=0 -d ./letti/ -W ./test/sample.txt -l ./test/sample.txt -u ./test/sample.txt -c ./test/sample.txt &
./client -p -t 200 -f filesocket -W ./test/long1.txt,./test/long2.txt,./test/long3.txt -D ./esplusi/ -r ./test/sample.txt -d ./letti/ -W ./test/sample2.txt -l ./test/sample2.txt -u ./test/sample2.txt -u ./test/sample2.txt -c ./test/sample2.txt &
./client -p -t 200 -f filesocket -R n=3 -d ./letti/ -W ./test/sample2.txt -l ./test/sample.txt -u ./test/sample.txt -c ./test/sample.txt

sleep 0.3 &

kill -s HUP $PID
wait $PID

cat stat.log

exit 0
