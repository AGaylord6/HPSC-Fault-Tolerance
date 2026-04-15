#!/bin/bash
# Compress a given JPEG image, then pause so that we can analyze memory usage

mkdir -p compressed

img="input.jpg"

if [ -f "$img" ]; then
    echo "Optimizing $img..."
    # Lossless transformation
    # jpegtran -copy none -optimize -progressive -perfect \
        # -outfile "compressed/$img" "$img" || \
        # echo "Failed to optimize $img"

    # Lossy compression
    # https://linux.die.net/man/1/cjpeg
    cjpeg -quality 75 -optimize -outfile "compressed/$img" "$img" || \
        echo "Failed to compress $img"
fi

pid=$!
echo "Process ID: $pid"

# Wait briefly for process initialization
sleep 1

# send SIGTSTP signal to the process to pause it
kill -TSTP $pid
# For a 'hard' stop, send SIGSTOP:
# kill -STOP $pid

cat /proc/$pid/status

cat /proc/$pid/maps

cat /proc/$pid/pagemap | head -n 10

# To resume execution of the process, sent SIGCONT:
kill -CONT $pid
