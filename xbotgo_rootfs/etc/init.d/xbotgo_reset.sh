#!/bin/bash

SCRIPT_LOG_DIR="/userdata/logs"
LOG_FILE_NAME="xbotgo_reset_$(date +%Y-%m-%d).log"
LOG_FILE="${SCRIPT_LOG_DIR}/${LOG_FILE_NAME}"

show_help() {
    echo "Usage: $0 [OPTION]"
    echo "Clean up userdata partition with specified operation"
    echo
    echo "Options:"
    echo "  ota          Delete all files in /userdata/ota/ (ignore /userdata/ota/tmp)"
    echo "  coredump     Delete all files in /userdata/coredump/"
    echo "  7            Keep only last 7 days of logs"
    echo "  3            Keep only last 3 days of logs"
    echo "  1            Keep only last 1 day of logs"
    echo "  userdata     Delete all files in /userdata/"
    echo "  help         Show help"
    echo
}

init_log() {
    if [ ! -d "$SCRIPT_LOG_DIR" ]; then
        mkdir -p "$SCRIPT_LOG_DIR"
        chmod 755 "$SCRIPT_LOG_DIR"
        echo "[$(date +%Y-%m-%d\ %H:%M:%S)] Created log directory: $SCRIPT_LOG_DIR" >> "$LOG_FILE"
    fi
    echo -e "\n==================================================" >> "$LOG_FILE"
    echo "[$(date +%Y-%m-%d\ %H:%M:%S)] Script started with argument: $1" >> "$LOG_FILE"
    echo "[$(date +%Y-%m-%d\ %H:%M:%S)] Current system time: $(date)" >> "$LOG_FILE"
    echo "==================================================" >> "$LOG_FILE"
    echo "[$(date +%Y-%m-%d\ %H:%M:%S)] Script log path: $LOG_FILE"
}

delete_dir_contents() {
    local dir_path="$1"
    local ignore_subdir="${dir_path}/tmp"
    local log_msg="[$(date +%Y-%m-%d\ %H:%M:%S)]"
    echo -e "\n${log_msg} Starting to delete contents in: $dir_path (ignore $ignore_subdir)" | tee -a "$LOG_FILE"
    
    if [ -d "$dir_path" ]; then
        echo "${log_msg} Before deletion, contents of $dir_path:" | tee -a "$LOG_FILE"
        local file_count_before=$(ls "$dir_path" | wc -l)
        echo "${log_msg} File count: $file_count_before" | tee -a "$LOG_FILE"
        ls "$dir_path" -lh | tee -a "$LOG_FILE"
        
        echo "${log_msg} Deleting all contents except $ignore_subdir..." | tee -a "$LOG_FILE"
        find "$dir_path" -mindepth 1 -maxdepth 1 ! -path "$ignore_subdir" -exec rm -rf {} +
        
        echo "${log_msg} After deletion, contents of $dir_path:" | tee -a "$LOG_FILE"
        local file_count_after=$(ls "$dir_path" | wc -l)
        echo "${log_msg} File count: $file_count_after" | tee -a "$LOG_FILE"
        ls "$dir_path" -lh | tee -a "$LOG_FILE"
        
        if [ -d "$ignore_subdir" ]; then
            echo "${log_msg} Successfully ignored subdirectory: $ignore_subdir" | tee -a "$LOG_FILE"
        else
            echo "${log_msg} Ignored subdirectory $ignore_subdir does not exist (no action needed)" | tee -a "$LOG_FILE"
        fi
        echo "${log_msg} Contents of $dir_path have been deleted (excluding $ignore_subdir, deleted $((file_count_before - file_count_after)) items)" | tee -a "$LOG_FILE"
    else
        echo "${log_msg} Error: $dir_path does not exist" | tee -a "$LOG_FILE"
        return 1
    fi
}

keep_last_n_days() {
    local dir_path="$1"
    local days="$2"
    local file_count=0
    local delete_count=0
    local log_msg="[$(date +%Y-%m-%d\ %H:%M:%S)]"

    echo -e "\n${log_msg} Starting log cleanup: keep last $days days in $dir_path" | tee -a "$LOG_FILE"
    
    if [ ! -d "$dir_path" ]; then
        echo "${log_msg} Error: Log directory $dir_path does not exist" | tee -a "$LOG_FILE"
        return 1
    fi

    local current_timestamp=$(date +%s)
    local sec_per_day=$((24 * 3600))
    local cutoff_timestamp=$((current_timestamp - days * sec_per_day))
    local current_date=$(date -d @$current_timestamp +%Y%m%d)
    local cutoff_date=$(date -d @$cutoff_timestamp +%Y%m%d)

    if [ -z "$current_date" ] || [ ${#current_date} -ne 8 ]; then
        echo "${log_msg} Error: Failed to get valid current date (format YYYYMMDD)" | tee -a "$LOG_FILE"
        return 1
    fi
    if [ -z "$cutoff_date" ] || [ ${#cutoff_date} -ne 8 ]; then
        echo "${log_msg} Error: Failed to calculate cutoff date (current - $days days)" | tee -a "$LOG_FILE"
        return 1
    fi

    echo "${log_msg} Current date (YYYYMMDD): $current_date" | tee -a "$LOG_FILE"
    echo "${log_msg} Cutoff date (keep files >= this date): $cutoff_date" | tee -a "$LOG_FILE"
    
    echo "${log_msg} Before cleanup, contents of $dir_path:" | tee -a "$LOG_FILE"
    local file_count_before=$(ls "$dir_path" | wc -l)
    echo "${log_msg} File count: $file_count_before" | tee -a "$LOG_FILE"
    ls "$dir_path" -lh | tee -a "$LOG_FILE"

    echo "${log_msg} Starting cleanup: keep logs >= $cutoff_date, delete logs < $cutoff_date (ignore file_mng* logs)..." | tee -a "$LOG_FILE"
    for file in "$dir_path"/*; do
        if [ -d "$file" ]; then
            continue
        fi
        if [ ! -f "$file" ]; then
            continue
        fi

        file_count=$((file_count + 1))
        filename=$(basename "$file")
        
        if [[ "$filename" == file_mng* ]]; then
            continue
        fi

        file_date=""
        if [[ "$filename" =~ ([0-9]{8}) ]]; then
            file_date="${BASH_REMATCH[1]}"
        elif [[ "$filename" =~ ([0-9]{4})-([0-9]{2})-([0-9]{2}) ]]; then
            file_date="${BASH_REMATCH[1]}${BASH_REMATCH[2]}${BASH_REMATCH[3]}"
        fi

        if [ -z "$file_date" ] || [ ${#file_date} -ne 8 ]; then
            echo "${log_msg} Warning: Cannot extract date from file $filename -> keep (unrecognized format)" | tee -a "$LOG_FILE"
            continue
        fi

        if [ "$file_date" -lt "$cutoff_date" ]; then
            rm -f "$file"
            if [ $? -eq 0 ]; then
                echo "${log_msg} Deleted: $filename (file date $file_date < cutoff $cutoff_date)" | tee -a "$LOG_FILE"
                delete_count=$((delete_count + 1))
            else
                echo "${log_msg} Error: Failed to delete $filename" | tee -a "$LOG_FILE"
            fi
        else
            echo "${log_msg} Kept: $filename (file date $file_date >= cutoff $cutoff_date)" | tee -a "$LOG_FILE"
        fi
    done

    find "$dir_path" -type d -empty -exec rm -rf {} +
    echo "${log_msg} Cleaned up empty directories in $dir_path" | tee -a "$LOG_FILE"

    echo -e "\n${log_msg} Cleanup result:" | tee -a "$LOG_FILE"
    echo "${log_msg} Total log files scanned: $file_count" | tee -a "$LOG_FILE"
    echo "${log_msg} Total files deleted: $delete_count" | tee -a "$LOG_FILE"
    
    echo "${log_msg} After cleanup, contents of $dir_path:" | tee -a "$LOG_FILE"
    local file_count_after=$(ls "$dir_path" | wc -l)
    echo "${log_msg} File count: $file_count_after" | tee -a "$LOG_FILE"
    ls "$dir_path" -lh | tee -a "$LOG_FILE"
    
    echo "${log_msg} Preserved logs from last $days days (by filename date) in $dir_path" | tee -a "$LOG_FILE"
}

if [ $# -eq 0 ]; then
    echo "Error: No operation specified"
    show_help
    exit 1
fi

init_log "$1"

case "$1" in
    other)
        find /userdata -mindepth 1 -maxdepth 1 \
             ! -name "ota" \
             ! -name "coredump" \
             ! -name "logs" \
             -exec rm -rf {} +
        ;;
    ota)
        delete_dir_contents "/userdata/ota"
        ;;
    coredump)
        delete_dir_contents "/userdata/coredump"
        ;;
    7|3|1)
        keep_last_n_days "/userdata/logs" "$1"
        echo -e "\n[$(date +%Y-%m-%d\ %H:%M:%S)] Cleaning script's own logs (keep last $1 days)..." | tee -a "$LOG_FILE"
        keep_last_n_days "$SCRIPT_LOG_DIR" "$1"
        ;;
    userdata)
        delete_dir_contents "/userdata"
        ;;
    help)
        show_help
        show_help >> "$LOG_FILE"
        exit 0
        ;;
    *)
        echo "Error: Invalid option '$1'" | tee -a "$LOG_FILE"
        show_help >> "$LOG_FILE"
        exit 1
        ;;
esac

echo -e "\n[$(date +%Y-%m-%d\ %H:%M:%S)] Script execution completed" | tee -a "$LOG_FILE"
echo "==================================================" | tee -a "$LOG_FILE"

exit 0
