/*
Utils for parsing physical memory info about a running process

https://www.lukas-barth.net/blog/linux-inspect-page-table/

Other method: using the macros pgd_offset_k, pud_offset, pmd_offset, pte_offset in sequence, you should be able to get the physical address.
*/

#include <cstdint>
#include <cstdio>
#include <cstdlib>

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

