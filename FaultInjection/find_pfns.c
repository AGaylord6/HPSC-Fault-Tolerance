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

#define PAGEMAP_PRESENT (UINT64_C(1) << 63)
#define PAGEMAP_SWAPPED (UINT64_C(1) << 62)
#define PAGEMAP_PFN_MASK ((UINT64_C(1) << 55) - 1)

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
    fprintf(stderr, "find_pfns requires Linux (/proc/<pid>/maps and /proc/<pid>/pagemap).\n");
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
    char line[LINE_BUFSZ];
    size_t mappings_considered = 0;
    size_t pages_scanned = 0;
    size_t present_pages = 0;
    size_t swapped_pages = 0;
    size_t no_pfn_pages = 0;
    size_t pages_written = 0;

    while (fgets(line, sizeof(line), maps) != NULL) {
        uint64_t start = 0;
        uint64_t end = 0;
        if (sscanf(line, "%" SCNx64 "-%" SCNx64, &start, &end) != 2) {
            continue;
        }

        if (!mapping_matches(line, target)) {
            continue;
        }

        printf("Considering mapping: %s", line);

        mappings_considered++;
        for (uint64_t addr = start; addr < end; addr += page_size) {
            pages_scanned++;
            uint64_t entry = read_pagemap_entry(pagemap, addr, page_size);
            if ((entry & PAGEMAP_PRESENT) == 0) {
                if (entry & PAGEMAP_SWAPPED) {
                    swapped_pages++;
                }
                continue;
            }

            present_pages++;

            uint64_t pfn = entry & PAGEMAP_PFN_MASK;
            if (pfn == 0) {
                no_pfn_pages++;
                continue;
            }

            printf("Found PFN=0x%" PRIx64 " for vaddr=0x%" PRIx64 "\n", pfn, addr);
            pages_written++;
        }
    }

    fclose(out);
    fclose(pagemap);
    fclose(maps);

    fprintf(stderr,
            "Scanned %zu pages across %zu matching mappings. Present=%zu Swapped=%zu PFN_zero=%zu PFNs_written=%zu -> %s\n",
            pages_scanned,
            mappings_considered,
            present_pages,
            swapped_pages,
            no_pfn_pages,
            pages_written,
            output_path);

        if (present_pages > 0 && pages_written == 0) {
        fprintf(stderr,
            "Warning: present pages found, but PFNs were zero. This usually means pagemap PFNs are hidden by kernel policy (CAP_SYS_ADMIN required).\n");
        }

    return 0;
#endif
}
