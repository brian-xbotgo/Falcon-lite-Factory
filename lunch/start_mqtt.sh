#!/bin/sh

LOG_DIR="/userdata/logs"
mkdir -p $LOG_DIR

# 生成时间戳
TS=$(date +"%Y%m%d_%H%M%S")

LOG_FILE="$LOG_DIR/mosquitto_$TS.log"

# 创建日志文件
touch "$LOG_FILE"
chmod 666 "$LOG_FILE"

# 启动 mosquitto 指定日志文件
exec /usr/bin/mosquitto -c /etc/mosquitto.conf -v 2>>"$LOG_FILE" >>"$LOG_FILE"
