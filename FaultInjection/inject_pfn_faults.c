/*
Given a file containing a list of PFNs (one per line), randomly flip bits in the corresponding physical memory pages.

Requires LKM to be loaded (with ioctl exposed)
*/
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/ioctl.h>
#include "faultmem/include/faultmem.h"

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s <pfn_file> <flips_per_pfn> [--dry-run]\n", prog);
}

int main(int argc, char **argv) {
    if (argc < 3 || argc > 4) {
        usage(argv[0]);
        return 1;
    }

    const char *pfn_file = argv[1];
    char *end = NULL;
    long flips_per_pfn = strtol(argv[2], &end, 10);
    if (*argv[2] == '\0' || (end && *end != '\0') || flips_per_pfn <= 0) {
        fprintf(stderr, "Invalid flips_per_pfn: %s\n", argv[2]);
        return 2;
    }

    bool dry_run = false;
    if (argc == 4) {
        if (strcmp(argv[3], "--dry-run") == 0) {
            dry_run = true;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[3]);
            return 3;
        }
    }

    FILE *in = fopen(pfn_file, "r");
    if (!in) {
        fprintf(stderr, "Failed to open %s: %s\n", pfn_file, strerror(errno));
        return 4;
    }

    int mem_fd = -1;
    long raw_page_size = sysconf(_SC_PAGESIZE);
    const uint64_t page_size = raw_page_size > 0 ? (uint64_t)raw_page_size : UINT64_C(4096);
    if (!dry_run) {
        if (geteuid() != 0) {
            fprintf(stderr, "Live mode requires root privileges (effective uid 0).\n");
            fclose(in);
            return 5;
        }

        // mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
        mem_fd = open("/dev/faultmem", O_RDWR);
        if (mem_fd < 0) {
            fprintf(stderr,
                    "Failed to open /dev/faultmem: %s (run as root, and ensure kernel policy allows access)\n",
                    strerror(errno));
            fclose(in);
            return 6;
        }
    }

    srand((unsigned int)time(NULL));

    size_t total_pfns = 0;
    size_t total_flips = 0;
    char line[256];

    while (fgets(line, sizeof(line), in) != NULL) {
        if (line[0] == '#' || line[0] == '\n') {
            continue;
        }

        char *line_end = NULL;
        errno = 0;
        uint64_t pfn = strtoull(line, &line_end, 0);
        if (errno != 0 || line_end == line) {
            continue;
        }

        total_pfns++;
        for (long i = 0; i < flips_per_pfn; ++i) {
            uint64_t phys_addr = pfn * page_size;
            uint64_t byte_offset = (uint64_t)(rand() % (int)page_size);
            uint8_t bit_index = (uint8_t)(rand() % 8);
            off_t target = (off_t)(phys_addr + byte_offset);

            if (dry_run) {
                printf("[dry-run] PFN=0x%" PRIx64 " addr=0x%" PRIx64 " bit=%u\n",
                       pfn,
                       (uint64_t)target,
                       bit_index);
                total_flips++;
                continue;
            }

            // uint8_t value = 0;
            // if (pread(mem_fd, &value, sizeof(value), target) != (ssize_t)sizeof(value)) {
            //     fprintf(stderr, "pread failed at 0x%" PRIx64 ": %s\n", (uint64_t)target, strerror(errno));
            //     continue;
            // }

            // // flip selected bit by XORing
            // value ^= (uint8_t)(1u << bit_index);
            // if (pwrite(mem_fd, &value, sizeof(value), target) != (ssize_t)sizeof(value)) {
            //     fprintf(stderr, "pwrite failed at 0x%" PRIx64 ": %s\n", (uint64_t)target, strerror(errno));
            //     continue;
            // }

            struct bit_flip_request req = {
                .phys_addr = target,
                .bit = bit_index
            };

            // Send ioctl to kernel module to flip the bit at the specified physical address
            if (ioctl(mem_fd, FAULTMEM_BIT_FLIP, &req) != 0) {
                fprintf(stderr, "ioctl failed at 0x%" PRIx64 ": %s\n", (uint64_t)target, strerror(errno));
                continue;
            }

            printf("Flipped bit %u at physical address 0x%" PRIx64 " (PFN=0x%" PRIx64 ")\n",
                   bit_index,
                   (uint64_t)target,
                   pfn);

            total_flips++;
        }
    }

    if (mem_fd >= 0) {
        close(mem_fd);
    }
    fclose(in);

    fprintf(stderr,
            "Processed %zu PFNs and attempted %zu flips (%s mode).\n",
            total_pfns,
            total_flips,
            dry_run ? "dry-run" : "live");

    return 0;
}
