/*
Given a process ID and a target string, find the PFNs of all pages mapped by that process into the specified VMA region

Outputs present/valid PFNs to the specified output file, one per line.
*/
#define _GNU_SOURCE
#include <errno.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#define LINE_BUFSZ 4096

#define PAGEMAP_PRESENT (UINT64_C(1) << 63)
#define PAGEMAP_SWAPPED (UINT64_C(1) << 62)
#define PAGEMAP_PFN_MASK ((UINT64_C(1) << 55) - 1)

// Struct for storing target VMA specifications parsed from the target string.
// Each spec has a pattern to match against the VMA line,
// and an optional 0-based occurrence index for disambiguation.
// Ex: "heap 0" would match the first VMA with "heap" in its description.
typedef struct {
    char *pattern;
    uint64_t occurrence;
    uint64_t seen_matches;
    bool has_occurrence;
} target_spec_t;

static char *trim_whitespace(char *text) {
    if (text == NULL) {
        return NULL;
    }

    while (isspace((unsigned char)*text)) {
        text++;
    }

    if (*text == '\0') {
        return text;
    }

    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';

    return text;
}

static void free_target_specs(target_spec_t *specs, size_t count) {
    if (specs == NULL) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        free(specs[i].pattern);
    }
    free(specs);
}

static bool parse_target_item(const char *item_text, target_spec_t *spec) {
    char *copy = strdup(item_text);
    if (copy == NULL) {
        return false;
    }

    char *item = trim_whitespace(copy);
    if (*item == '\0') {
        free(copy);
        return false;
    }

    char *end = item + strlen(item);
    while (end > item && !isspace((unsigned char)end[-1])) {
        end--;
    }

    if (end > item) {
        char *candidate = trim_whitespace(end);
        char *parse_end = NULL;
        errno = 0;
        unsigned long long occurrence = strtoull(candidate, &parse_end, 10);
        if (errno == 0 && parse_end != candidate && *trim_whitespace(parse_end) == '\0') {
            *end = '\0';
            char *pattern = trim_whitespace(item);
            if (*pattern == '\0') {
                free(copy);
                return false;
            }

            spec->pattern = strdup(pattern);
            if (spec->pattern == NULL) {
                free(copy);
                return false;
            }

            spec->occurrence = (uint64_t)occurrence;
            spec->seen_matches = 0;
            spec->has_occurrence = true;
            free(copy);
            return true;
        }
    }

    spec->pattern = strdup(item);
    if (spec->pattern == NULL) {
        free(copy);
        return false;
    }

    spec->occurrence = 0;
    spec->seen_matches = 0;
    spec->has_occurrence = false;
    free(copy);
    return true;
}

static bool parse_target_specs(const char *target, target_spec_t **specs_out, size_t *count_out) {
    *specs_out = NULL;
    *count_out = 0;

    char *copy = strdup(target);
    if (copy == NULL) {
        return false;
    }

    target_spec_t *specs = NULL;
    size_t count = 0;
    char *cursor = copy;

    while (cursor != NULL) {
        char *next = strchr(cursor, ',');
        if (next != NULL) {
            *next = '\0';
            next++;
        }

        char *item = trim_whitespace(cursor);
        if (*item != '\0') {
            target_spec_t spec = {0};
            if (!parse_target_item(item, &spec)) {
                free_target_specs(specs, count);
                free(copy);
                return false;
            }

            target_spec_t *grown = realloc(specs, (count + 1) * sizeof(*grown));
            if (grown == NULL) {
                free(spec.pattern);
                free_target_specs(specs, count);
                free(copy);
                return false;
            }

            specs = grown;
            specs[count++] = spec;
        }

        cursor = next;
    }

    free(copy);
    *specs_out = specs;
    *count_out = count;
    return true;
}

static bool mapping_has_empty_label(const char *line) {
    char *copy = strdup(line);
    if (copy == NULL) {
        return false;
    }

    char *cursor = copy;
    // Skip required fields: addr perms offset dev inode
    for (int i = 0; i < 5; i++) {
        cursor = strtok(i == 0 ? cursor : NULL, " \t\n");
        if (cursor == NULL) {
            free(copy);
            return false;
        }
    }

    char *rest = strtok(NULL, "\n");
    bool is_empty_label = (rest == NULL) || (*trim_whitespace(rest) == '\0');
    free(copy);
    return is_empty_label;
}

static bool mapping_matches_spec(const char *line, target_spec_t *spec) {
    if (strcmp(spec->pattern, "all") == 0) {
        return true;
    }

    if (strcmp(spec->pattern, "heap") == 0) {
        if (strstr(line, "[heap]") == NULL) {
            return false;
        }
    } else if (strcmp(spec->pattern, "anon") == 0) {
        if (!mapping_has_empty_label(line)) {
            return false;
        }
    } else if (strstr(line, spec->pattern) == NULL) {
        // Check if pattern is a substring of the line
        return false;
    }

    if (!spec->has_occurrence) {
        return true;
    }

    bool is_selected_occurrence = (spec->seen_matches == spec->occurrence);
    spec->seen_matches++;
    return is_selected_occurrence;
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

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <pid> <target: heap|anon|all|substring[, substring...]> <output_file>\n", argv[0]);
        return 1;
    }

    const char *pid = argv[1];
    const char *target = argv[2];
    const char *output_path = argv[3];

    // Create list of target VMA specifications from target string
    target_spec_t *target_specs = NULL;
    size_t target_spec_count = 0;
    if (!parse_target_specs(target, &target_specs, &target_spec_count) || target_spec_count == 0) {
        fprintf(stderr, "Invalid target specification: %s\n", target);
        return 1;
    }

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

    // Read each mapping from /proc/<pid>/maps
    while (fgets(line, sizeof(line), maps) != NULL) {
        uint64_t start = 0;
        uint64_t end = 0;
        if (sscanf(line, "%" SCNx64 "-%" SCNx64, &start, &end) != 2) {
            continue;
        }

        // Only consider target VMA ranges
        bool mapping_selected = false;
        for (size_t i = 0; i < target_spec_count; i++) {
            if (mapping_matches_spec(line, &target_specs[i])) {
                mapping_selected = true;
                break;
            }
        }

        if (!mapping_selected) {
            continue;
        }

        printf("Considering mapping: %s", line);

        mappings_considered++;
        // Find physical page frame for each page in mapping
        for (uint64_t addr = start; addr < end; addr += page_size) {
            pages_scanned++;
            uint64_t entry = read_pagemap_entry(pagemap, addr, page_size);
            // Check if page is present (mapped into RAM)
            if ((entry & PAGEMAP_PRESENT) == 0) {
                if (entry & PAGEMAP_SWAPPED) {
                    swapped_pages++;
                }
                continue;
            }

            present_pages++;

            uint64_t pfn = entry & PAGEMAP_PFN_MASK;
            // If PFN is zero, page is hidden
            if (pfn == 0) {
                no_pfn_pages++;
                continue;
            }

            //printf("Found PFN=0x%" PRIx64 " for vaddr=0x%" PRIx64 "\n", pfn, addr);
            fprintf(out, "%" PRIu64 "\n", pfn);
            pages_written++;
        }
    }

    fclose(out);
    fclose(pagemap);
    fclose(maps);
    free_target_specs(target_specs, target_spec_count);

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
}
