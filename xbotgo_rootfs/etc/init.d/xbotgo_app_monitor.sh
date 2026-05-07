#!/bin/sh
###
 # @Author: brian-xbotgo hubiguang@xbotgo.com
 # @Date: 2025-08-21 11:57:45
 # @LastEditors: brian-xbotgo hubiguang@xbotgo.com
 # @LastEditTime: 2025-08-27 22:01:40
### 

PROCESS1="ota_update"
CHECK_INTERVAL=10
MAX_RESTART_ATTEMPTS=7  
RESTART_COUNT_FILE="/userdata/ota_restart_count" 

SCRIPT_LOG_DIR="/userdata/logs"
LOG_FILE_NAME="xbotgo_app_monitor_$(date +%Y-%m-%d).log"
LOG_FILE="${SCRIPT_LOG_DIR}/${LOG_FILE_NAME}"

mkdir -p "$(dirname "$LOG_FILE")"

get_restart_count() {
    if [ -f "$RESTART_COUNT_FILE" ]; then
        cat "$RESTART_COUNT_FILE"
    else
        echo 0
    fi
}

update_restart_count() {
    local count=$1
    echo "$count" > "$RESTART_COUNT_FILE"
}

check_process() {
    local proc_name="$1"
    if ps | grep -v grep | grep -w "$proc_name" > /dev/null; then
        return 0
    else
        return 1
    fi
}

while true; do
    timestamp=$(date +'%Y-%m-%d %H:%M:%S')
    current_count=$(get_restart_count)
    
    if check_process "$PROCESS1"; then
        process1_exists=1
        status1="running"
        # 进程正常运行，重置计数器
        update_restart_count 0
    else
        process1_exists=0
        status1="not running"
        # 进程不存在，增加计数器
        current_count=$((current_count + 1))
        update_restart_count $current_count
    fi

    echo "[$timestamp] Check status - $PROCESS1: $status1 (Restart attempts: $current_count/$MAX_RESTART_ATTEMPTS)" >> "$LOG_FILE"

    if [ $process1_exists -eq 0 ]; then
        if [ $current_count -ge $MAX_RESTART_ATTEMPTS ]; then
            echo "[$timestamp] Reached max restart attempts ($MAX_RESTART_ATTEMPTS). Executing updateEngine --misc=other" >> "$LOG_FILE"
            updateEngine --misc=other >> "$LOG_FILE"
            update_restart_count 0
            echo "[$timestamp] Rebooting after special operation" >> "$LOG_FILE"
            reboot
        else
            echo "[$timestamp] Process not running, rebooting (attempt $current_count)" >> "$LOG_FILE"
            updateEngine --misc=display >> "$LOG_FILE"
            reboot
        fi
    else
        echo "[$timestamp] Process running normally, skipping operation" >> "$LOG_FILE"
    fi

    sleep $CHECK_INTERVAL
done
