#!/bin/bash
set -e

# Remove module only if it's currently loaded
if lsmod | grep -q '^faultmem\b'; then
	echo "faultmem is loaded, removing"
	sudo rmmod faultmem
fi

echo "Building faultmem module"
make

echo "Inserting module"
sudo insmod faultmem.ko

echo
sudo lsmod | grep faultmem || echo "faultmem not present in lsmod."
