## Introduction
Source: https://github.com/spacemit-com/riscv-ime-extension-spec/tree/master/example

This is a sample demo to use spacemit IME extension instruction. 

This example uses the vmadot instruction, which performs a matrix multiplication.
The instruction can been viewed in the injected assembly instructions on line 85 of "vmadot-gemm-demo.c".
    "vmadot       v28, v0, v1             \n\t"

For spacemiT k1 with bainbuOS, gcc has toolchain preinstalled. Only need to enter the following command to compile the program: `gcc -march=rv64gcv vmadot-gemm-demo.c -o gemm-vmadot-4x8x4`
