#!/bin/bash
sudo rmmod faultmem.ko
make
sudo insmod faultmem.ko
echo "\n"
sudo lsmod | grep faultmem
