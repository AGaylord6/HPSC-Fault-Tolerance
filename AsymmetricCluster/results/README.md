## Asymmetry Results

The screenshot titled "gcc_fail" shows compiling the vmadot program without the "-march=rv64gcv" flag, which then fails on run for unrecognized opcodes.
This flag tells the compilier to use the RISC-V vector extensions, which on Bianbu also includes the custom spacemiT instructions.

The screenshot titled "gcc_run" shows a successful compilation and run of the vmadot program.
The screenshot titled "core_0" shows using the "taskset -c 0" command to force the CPU to run the program on core 0, which it completes sucessfully.
The screenshot titled "core_4" shows that running the custom instrucions on cluster 1 (cores 4-7) will fail due to "Illegal Instruction" as expected. Only core 0 includes the custom spacemiT silicon, and therefore the instructions.