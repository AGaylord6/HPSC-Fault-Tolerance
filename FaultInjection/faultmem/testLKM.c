/*
Test the iotcl and pread/pwrite interfaces of kernel module
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include "include/faultmem.h"

int main() {

    // Create buffer of test data
    char buffer[256] = {0};
    for (size_t i = 0; i < sizeof(buffer); ++i) {
        buffer[i] = (char)i;
    }

    printf("Original buffer value: 0x%02x\n", buffer[0]);

    // Find physical address of buffer using /proc/self/pagemap
    FILE *pagemap = fopen("/proc/self/pagemap", "rb");
    if (!pagemap) {
        perror("fopen");
        return 1;
    }
    long page_size = sysconf(_SC_PAGESIZE);
    uint64_t vaddr = (uint64_t)buffer;
    uint64_t index = (vaddr / page_size) * sizeof(uint64_t);
    if (fseeko(pagemap, (off_t)index, SEEK_SET) != 0) {
        perror("fseeko");
        fclose(pagemap);
        return 1;
    }
    uint64_t entry = 0;
    if (fread(&entry, sizeof(entry), 1, pagemap) != 1) {
        perror("fread");
        fclose(pagemap);
        return 1;
    }
    fclose(pagemap);
    if ((entry & (UINT64_C(1) << 63)) == 0) {
        fprintf(stderr, "Page not present\n");
        return 1;
    }
    uint64_t pfn = entry & ((UINT64_C(1) << 55) - 1);
    uint64_t phys_addr = (pfn * page_size) + (vaddr % page_size);

    // Read original value at physical address using pread on /dev/faultmem
    int mem_fd = open("/dev/faultmem", O_RDWR);
    if (mem_fd < 0) {
        perror("open");
        return 1;
    }

    uint8_t original_value = 0;
    if (pread(mem_fd, &original_value, sizeof(original_value), (off_t)phys_addr) != (ssize_t)sizeof(original_value)) {
        perror("pread");
        close(mem_fd);
        return 1;
    }

    printf("Original value at phys=0x%llx (VA=0x%llx): 0x%02x\n", (unsigned long long)phys_addr, (unsigned long long)buffer, original_value);

    // Flip a bit in the value and write back using pwrite
    uint8_t modified_value = original_value ^ 0x01; // Flip least significant bit
    if (pwrite(mem_fd, &modified_value, sizeof(modified_value), (off_t)phys_addr) != (ssize_t)sizeof(modified_value)) {
        perror("pwrite");
        close(mem_fd);
        return 1;
    }

    // Check original buffer value
    printf("Value in original buffer after pwrite: 0x%02x\n", buffer[0]);
    if (buffer[0] != modified_value) {
        fprintf(stderr, "Test failed: expected buffer[0] to be 0x%02x but got 0x%02x\n", modified_value, buffer[0]);
        close(mem_fd);
        return 1;
    }

    // Flip a different bit using ioctl
    struct bit_flip_request req = {
        .phys_addr = phys_addr,
        .bit = 1 // Flip second least significant bit
    };
    if (ioctl(mem_fd, FAULTMEM_BIT_FLIP, &req) != 0) {
        perror("ioctl");
        close(mem_fd);
        return 1;
    }

    // Check original buffer
    printf("Value in original buffer after ioctl: 0x%02x\n", buffer[0]);

    // Read back value using pread and verify both flips were successful
    uint8_t final_value = 0;
    if (pread(mem_fd, &final_value, sizeof(final_value), (off_t)phys_addr) != (ssize_t)sizeof(final_value)) {
        perror("pread");
        close(mem_fd);
        return 1;
    }
    printf("Final value at phys=0x%llx: 0x%02x\n", (unsigned long long)phys_addr, final_value);
    uint8_t expected_value = original_value ^ 0x03; // Both least significant bits flipped
    if (final_value != expected_value) {
        fprintf(stderr, "Test failed: expected 0x%02x but got 0x%02x\n", expected_value, final_value);
        close(mem_fd);
        return 1;
    }

    // Check that original buffer value is also updated
    printf("Value in original buffer: 0x%02x\n", buffer[0]);
    if (buffer[0] != expected_value) {
        fprintf(stderr, "Test failed: expected buffer[0] to be 0x%02x but got 0x%02x\n", expected_value, buffer[0]);
        close(mem_fd);
        return 1;
    }

     return 0;
}
