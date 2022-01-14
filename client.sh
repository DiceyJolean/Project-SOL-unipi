while true
do

./client -f filesocket -w ./test/Inferno1-10/ -D ./espulsi/ -R n=1 -d ./letti/ -W ./test/sample.txt -l ./test/sample.txt -u ./test/sample.txt -c ./test/sample.txt &
./client -f filesocket -W ./test/long1.txt,./test/long2.txt,./test/long3.txt -D ./espulsi/ -r ./test/sample.txt -d ./letti/ -W ./test/sample2.txt -l ./test/sample2.txt -u ./test/sample2.txt -c ./test/sample2.txt &
./client -f filesocket -R n=3 -d ./letti/ -W ./test/sample2.txt -l ./test/sample.txt -u ./test/sample.txt -c ./test/sample.txt &
./client -f filesocket -W ./test/Inferno11-20/Canto11.txt,./test/Inferno11-20/Canto12.txt,./test/Inferno11-20/Canto13.txt -D ./espulsi/ -l ./test/Inferno11-20/Canto11.txt -r ./test/Inferno11-20/Canto11.txt -d ./letti/ -u ./test/Inferno11-20/Canto11.txt &
./client -f filesocket -w ./test/Inferno11-20/ -D ./espulsi/ -W ./test/Inferno11-20/Canto20.txt -D ./espulsi/ -r ./test/long1.txt -d ./letti/ -c ./test/Inferno11-20/Canto11.txt,./test/Inferno11-20/Canto12.txt -r ./test/Inferno1-10/Canto5.txt &
./client -f filesocket -R n=10 -d ./letti/ -l ./test/Inferno1-10/Canto1.txt -r ./test/Inferno1-10/Canto1.txt -u ./test/Inferno1-10/Canto1.txt &
./client -f filesocket -c ./test/Inferno1-10/Canto1.txt,./test/Inferno1-10/Canto10.txt,./test/Inferno1-10/Canto2.txt,./test/Inferno1-10/Canto3.txt,./test/Inferno1-10/Canto4.txt &
./client -f filesocket -c ./test/Inferno1-10/Canto5.txt,./test/Inferno1-10/Canto6.txt,./test/Inferno1-10/Canto7.txt,./test/Inferno1-10/Canto8.txt,./test/Inferno1-10/Canto9.txt &
./client -f filesocket -c ./test/Inferno11-20/Canto11.txt,./test/Inferno11-20/Canto12.txt,./test/Inferno11-20/Canto13.txt,./test/Inferno11-20/Canto14.txt,./test/Inferno11-20/Canto15.txt &
./client -f filesocket -R n=0 ./test/Inferno11-20/Canto16.txt,./test/Inferno11-20/Canto17.txt,./test/Inferno11-20/Canto18.txt,./test/Inferno11-20/Canto19.txt,./test/Inferno11-20/Canto20.txt &
./client -f filesocket -W ./test/long1.txt,./test/long2.txt,./test/long3.txt,./test/long4.txt -D ./espulsi/ -r ./test/long1.txt,./test/long2.txt,./test/long3.txt,./test/long4.txt -d ./letti/

sleep 0.2

done
