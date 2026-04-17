#!/usr/bin/env bash

for file in ./*.jpg; do
	if [ -f "$file" ]; then
		ppm_file="${file%.*}.ppm"
		jpegtopnm "$file" > "$ppm_file"
	fi
done
