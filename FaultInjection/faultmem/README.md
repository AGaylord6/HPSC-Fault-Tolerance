## faultmeme

Custom Linux Kernel Module (LMK) for fault injection.

Provides read/write access to physical memory, circumventing the need for /dev/mem (which is protected by `CONFIG_STRICT_DEVMEM`)

Works on bainbuOS OS running on risc-v spacemiT K1 chip