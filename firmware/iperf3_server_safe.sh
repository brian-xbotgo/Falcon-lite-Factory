#!/bin/sh

PORT=5201
ZERO_LIMIT=5        # 连续 5 秒 0 流量则认为异常
LOG=/userdata/logs/iperf.log

echo "iperf3 safe server started..."

while true
do
    echo "==============================="
    echo "Starting iperf3 server..."

    iperf3 -s --logfile $LOG 2>&1 &
    IPERF_PID=$!

    ZERO_COUNT=0

    while kill -0 $IPERF_PID 2>/dev/null
    do
        # 取最近一行 bitrate
        LINE=$(tail -n 1 $LOG | grep "bits/sec")

        if echo "$LINE" | grep -q "0.00 bits/sec"; then
            ZERO_COUNT=$((ZERO_COUNT + 1))
            echo "Zero traffic detected ($ZERO_COUNT/$ZERO_LIMIT)"
        else
            ZERO_COUNT=0
        fi

        if [ $ZERO_COUNT -ge $ZERO_LIMIT ]; then
            echo "Traffic idle too long, killing iperf3"
            kill $IPERF_PID
            break
        fi

        sleep 1
    done

    wait $IPERF_PID 2>/dev/null
    echo "iperf3 server exited"

    sleep 1
done
