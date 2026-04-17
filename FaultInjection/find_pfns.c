#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <unistd.h>
#endif

#define LINE_BUFSZ 4096

#ifdef __linux__
static bool mapping_matches(const char *line, const char *target) {
    if (strcmp(target, "all") == 0) {
        return true;
    }
    if (strcmp(target, "heap") == 0) {
        return strstr(line, "[heap]") != NULL;
    }
    return strstr(line, target) != NULL;
}

static uint64_t read_pagemap_entry(FILE *pagemap, uint64_t vaddr, uint64_t page_size) {
    uint64_t index = (vaddr / page_size) * sizeof(uint64_t);
    if (fseeko(pagemap, (off_t)index, SEEK_SET) != 0) {
        return 0;
    }

    uint64_t entry = 0;
    if (fread(&entry, sizeof(entry), 1, pagemap) != 1) {
        return 0;
    }
    return entry;
}
#endif

int main(int argc, char **argv) {
#ifndef __linux__
    (void)argc;
    (void)argv;
    fprintf(stderr, "find_pfns is Linux-only (/proc/<pid>/maps and /proc/<pid>/pagemap required).\n");
    return 1;
#else
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <pid> <target: heap|all|substring> <output_file>\n", argv[0]);
        return 1;
    }

    const char *pid = argv[1];
    const char *target = argv[2];
    const char *output_path = argv[3];

    char maps_path[128];
    char pagemap_path[128];
    snprintf(maps_path, sizeof(maps_path), "/proc/%s/maps", pid);
    snprintf(pagemap_path, sizeof(pagemap_path), "/proc/%s/pagemap", pid);

    FILE *maps = fopen(maps_path, "r");
    if (!maps) {
        fprintf(stderr, "Failed to open %s: %s\n", maps_path, strerror(errno));
        return 2;
    }

    FILE *pagemap = fopen(pagemap_path, "rb");
    if (!pagemap) {
        fprintf(stderr, "Failed to open %s: %s\n", pagemap_path, strerror(errno));
        fclose(maps);
        return 3;
    }

    FILE *out = fopen(output_path, "w");
    if (!out) {
        fprintf(stderr, "Failed to open %s: %s\n", output_path, strerror(errno));
        fclose(maps);
        fclose(pagemap);
        return 4;
    }

    long raw_page_size = sysconf(_SC_PAGESIZE);
    const uint64_t page_size = raw_page_size > 0 ? (uint64_t)raw_page_size : UINT64_C(4096);
    const uint64_t pfn_mask = ((UINT64_C(1) << 55) - 1);

    char line[LINE_BUFSZ];
    size_t mappings_considered = 0;
    size_t present_pages = 0;

    while (fgets(line, sizeof(line), maps) != NULL) {
        uint64_t start = 0;
        uint64_t end = 0;
        if (sscanf(line, "%" SCNx64 "-%" SCNx64, &start, &end) != 2) {
            continue;
        }

        if (!mapping_matches(line, target)) {
            continue;
        }

        mappings_considered++;
        for (uint64_t addr = start; addr < end; addr += page_size) {
            uint64_t entry = read_pagemap_entry(pagemap, addr, page_size);
            if ((entry & (UINT64_C(1) << 63)) == 0) {
                continue;
            }

            uint64_t pfn = entry & pfn_mask;
            if (pfn == 0) {
                continue;
            }

            fprintf(out, "0x%" PRIx64 "\n", pfn);
            present_pages++;
        }
    }

    fclose(out);
    fclose(pagemap);
    fclose(maps);

    fprintf(stderr,
            "Wrote %zu PFNs from %zu matching mappings to %s\n",
            present_pages,
            mappings_considered,
            output_path);

    return 0;
#endif
}
