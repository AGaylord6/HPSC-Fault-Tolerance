#!/usr/bin/bash

# Begin and pause cjpeg process

# send SIGTSTP signal to the process to pause it
kill -TSTP $pid
# For a 'hard' stop, send SIGSTOP:
# kill -STOP $pid

# Call C program that takes in PID and target mapping (heap, file/library, address range, etc)
#   Read virtual memory maps
#   Find physical pages backing the target mappings

# Call C program that injects Single-event upset (SEU) into given PFN at given frequency (or address within it)

# Resume cjpeg process and observe for crashes, hangs, or output corruption
# To resume execution of the process, sent SIGCONT:
kill -CONT $pid
