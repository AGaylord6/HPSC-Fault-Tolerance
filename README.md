## HPSC-Fault-Tolerance

Computing in Spaceflight project in collaboration with NASA's High Performance Space Computer (HPSC) program.

This project explores fault tolerance on the Banana Pi BPI-F3 and its SpacemiT K1 8 core RISC-V chip. It implements a custom fault injector to explore the effect of Single Effect Upsets (SEU's) on flagship image compression methods.

Future work will involve novel fault tolerance methods that can overcome the corrupted memory.

***
### Members

* Andrew Gaylord

* Zach Kennedy

* Cole Descalzi

* Nicholas Morales

### Advisors

* Professor Matthew Morrison: Notre Dame professor of Computing in Spaceflight.

* Pete Fiaco: member of NASA/JPL's HPSC (High Performance Space Computer) Leadership Team.

***
### Structure

TODO: create `Setup/` folder with instructions from the Google docs

`Benchmarking/`: explore the speed, memory usage, and SIMD capabilities of image compression on this chip (WIP).

`FaultInjection/`: scripts that allow for automated fault injection into a running image compression process, including a custom Loadable Kernel Module (LKM) for Linux. 

`Dataset/`: contains JPEG and PPM versions of sample Google Earth images.

`AsymmetricCluster/`: examples that demonstrate the custom RISC-V Integrated Matrix Extension (IME) set that is supported by one of the SpacemiT clusters.

***
### Setup

See `Setup/README.md` for OS booting, environment setup
