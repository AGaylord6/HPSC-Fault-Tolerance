/*
Utils for parsing physical memory info about a running process

https://www.lukas-barth.net/blog/linux-inspect-page-table/

Other method: using the macros pgd_offset_k, pud_offset, pmd_offset, pte_offset in sequence, you should be able to get the physical address.
*/

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <vector>

size_t page_index(void *ptr) {
    return reinterpret_cast<size_t>(ptr) / 4096;
}

/*
Every page has 64-bit entry in /pagemap
The lower 54 bits of the entry contain the PFN (Page Frame Number)
*/
size_t get_pfn(void *ptr) {
    size_t pi = page_index(ptr);

    // /proc/self points to the procfs folder of the current process, so
    // we don't need to figure out our PID first.
    auto fp = std::fopen("/proc/self/pagemap", "rb");

    // Each entry in pagemap is 64 bits, i.e, 8 bytes.
    std::fseek(fp, pi * 8, SEEK_SET);

    // Read 64 bits into an uint64_t
    uint64_t page_info = 0;
    std::fread(&page_info, 8, 1, fp);

    // check if page is present (bit 63). Otherwise, there is no PFN.
    if (page_info & (static_cast<uint64_t>(1) << 63)) {
        // Create a mask that has ones in the lowest 54 bits, and use that to extract the PFN.
        uint64_t pfn = page_info & ((static_cast<uint64_t>(1) << 55) - 1);
        return static_cast<size_t>(pfn);
    } else {
        // page not present
        return 0;
    }
}

/*
/kpageflags has 64-bit entries for each page frame, where each bit represents a flag about the page
*/
size_t get_pflags(void *ptr) {
    size_t pfn = get_pfn(ptr);
    if (pfn == 0) { return 0; } // non-present pages

    auto fp = std::fopen("/proc/kpageflags", "rb");
    std::fseek(fp, pfn * 8, SEEK_SET);

    uint64_t pflags = 0;
    auto result = std::fread(&pflags, 8, 1, fp);

    return static_cast<size_t>(pflags);
}

bool present_from_pte(void *ptr) {
    // vvvvvv same as get_pfn() vvvvvv
    size_t pi = page_index(ptr);
    auto fp = std::fopen("/proc/self/pagemap", "rb");
    std::fseek(fp, pi * 8, SEEK_SET);
    uint64_t page_info = 0;
    std::fread(&page_info, 8, 1, fp);
    // ^^^^^^ same as get_pfn() ^^^^^^

    // Bit 63 is the 'present' bit
    return page_info & (static_cast<uint64_t>(1) << 63);
}

std::vector<std::pair<size_t, size_t>> get_heap_ranges() {
    // This only matches lines ending in "[heap]", and captures the respective
    // start/end address in the first/second group.
    std::regex re(
        "([0-9a-f]*)-([0-9a-f]*) .{4} [0-9a-f]* .{2}:.{2} [0-9]* *\\[heap\\]");
    std::ifstream mapsfile("/proc/self/maps");
    std::string line;

    std::vector<std::pair<size_t, size_t>> result;
    while (std::getline(mapsfile, line)) {
        std::smatch match_result;
        bool matched = std::regex_match(line, match_result, re);
        if (!matched) {
            // not a "heap" line
            continue;
        }

        // start/end addresses are in hex, so we use base 16 here
        size_t start = std::stoul(match_result[1].str(), nullptr, 16);
        size_t end = std::stoul(match_result[2].str(), nullptr, 16);
        result.emplace_back(start, end);
    }

    return result;
}

/*
Count how many pages in the heap are present (in RAM), by 'probing' one address from each page and checking if it's present.
*/
size_t count_present_in_heap() {
    size_t present_count = 0;
    size_t total = 0;

    auto ranges = get_heap_ranges();
    for (auto [heap_start, heap_end] : ranges) {
        // We 'probe' in 4kB, i.e., page size, steps, so we apply present_from_pte() to one address from
        // each page each.
        for (size_t current = heap_start; current < heap_end; current += 4096) {
            total++;

            if (present_from_pte(reinterpret_cast<void *>(current))) {
                present_count++;
            }
        }
    }

    std::cout << "Counted " << present_count << " pages present in " << total
            << " pages of heap memory.\n";
    return present_count;
}

int main() {
    count_present_in_heap();
    return 0;
}
