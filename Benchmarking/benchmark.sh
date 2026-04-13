#!/bin/bash
# Benchmark default JPEG compression on our 8-core risc-v chip

# jpeglib-turbo provides its own benchmarking tool to measure compression speed, decompression speed, and scaling
tjbench test.jpg 75