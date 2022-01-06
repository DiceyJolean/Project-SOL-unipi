./filestorage -f config3.txt &
SERVER_PID=$!
#echo "SERVER PID = $SERVER_PID"

bash -c './client.sh' &
CLIENT_PID=$!
#echo "CLIENT PID = $CLIENT_PID"

echo "Test 3 is running, wait 30 seconds..."
sleep 30 && kill -3 ${SERVER_PID}

wait ${SERVER_PID}
kill ${CLIENT_PID}

exit 0