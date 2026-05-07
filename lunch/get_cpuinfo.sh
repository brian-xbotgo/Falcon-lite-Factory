#!/bin/sh

serial=$(awk '/Serial/ {print $NF}' /proc/cpuinfo)
#check whether the file exists, and if it does not, create and write the cpu serial info
filename="/userdata/cpuinfo.txt"
if [ ! -f "$filename" ]; then
    echo "$serial" > "$filename"
else
    #if the file exists, the cpuinfo inside is read for comparison
    existing_serial=$(cat "$filename")
    if [ "$existing_serial" != "$serial" ]; then
        #if the cpuinfo in the file is different from the extracted one, the original file is overwritten
        echo "$serial" > "$filename"
        echo "new cpuinfo has been written to file!"
    else
        echo "the same cpuinfo already exists in the file!"
    fi
fi
