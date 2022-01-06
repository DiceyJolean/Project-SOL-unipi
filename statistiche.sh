# Erica Pistolesi 518169

#!/bin/bash

LOG_FILE=$1 # Salvo il nome del file di log

# cat $LOG_FILE

READ_NO=$(awk '/READ/ {print}' "${LOG_FILE}" | wc -l)
echo "Numero di READ_FILE e READ_N_FILES eseguite: $READ_NO"

WRITE_NO=$(awk '/WRITE/ {print}' "${LOG_FILE}" | wc -l)
echo "Numero di WRITE_FILE e APPEND_FILE eseguite: $WRITE_NO"

LOCK_NO=$(awk '/ LOCK/ {print}' "${LOG_FILE}" | wc -l)
echo "Numero di LOCK_FILE eseguite: $LOCK_NO"

UNLOCK_NO=$(awk '/UNLOCK/ {print}' "${LOG_FILE}" | wc -l)
echo "Numero di UNLOCK_FILE eseguite: $UNLOCK_NO"

OPENLOCK_NO=$(awk '/OPEN_FILE\(OLOCK/ {print}' "${LOG_FILE}" | wc -l)
echo "Numero di OPEN_FILE con LOCK eseguite: $OPENLOCK_NO"

CLOSE_NO=$(awk '/CLOSE/ {print}' "${LOG_FILE}" | wc -l)
echo "Numero di CLOSE_FILE eseguite: $CLOSE_NO"

CACHE_MISS_NO=$(awk '/CACHE_MISS/ {print}' "${LOG_FILE}" | wc -l)
echo "Numero di CACHE_MISS eseguite: $CACHE_MISS_NO"

MAX=$(awk '/MAX/' ${LOG_FILE})
REMAINING=$(awk '/RIMANENTI/' "$LOG_FILE")
WORKER=$(awk '/eseguite/' "$LOG_FILE")

echo "$MAX"
echo "$REMAINING"
echo "$WORKER"

READ_BYTES=$(awk -F ' ' '$4 ~ /READ/ {sum += $10} END {print sum}' $LOG_FILE)
echo "Bytes letti in media: $(($READ_BYTES / $READ_NO))"
WRITE_BYTES=$(awk -F ' ' '$4 ~ /WRITE/ || /APPEND/ {sum += $10} END {print sum}' $LOG_FILE)
echo "Bytes scritti in media: $(($WRITE_BYTES / $WRITE_NO))"

exit 0