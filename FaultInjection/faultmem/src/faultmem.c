/*
faultmem.c

Custom Linux Kernel Module (LMK) for fault injection.

Provides read/write access to physical memory, circumventing the need for /dev/mem (which is protected by `CONFIG_STRICT_DEVMEM`)

Works on bainbuOS OS running on risc-v spacemiT K1 chip

References:
* https://linux.die.net/lkmpg/x892.html
* https://github.com/NateBrune/fmem/blob/master/lkm.c (loosely)
* https://www.kernel.org/doc/html/latest/userspace-api/ioctl/ioctl-number.html
* https://sysprog21.github.io/lkmpg/ chapter 9
*/

// preclude any kernel prints with LMK name + function name
#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/page.h>
#include <linux/uaccess.h>

#include <linux/io.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/atomic.h>

#include "faultmem.h"

MODULE_LICENSE("GPL");

/**
 * A module-global describing whether the device is open or not.
 *
 * Prevents race conditions from multiple processes
 */
static atomic_t device_is_open = ATOMIC_INIT(0);

/**
 * Does loop processing for read and write calls.
 * Finds the appriorate length (avoiding page boundaries).
 * Returns correct virtual address in kernel space in @mappedaddr
 */
static ssize_t get_len_and_mapped_addr(void **mapped_addr, phys_addr_t phys_addr, ssize_t remaining) {
    unsigned long pfn;
    // check where we are within a page
    ssize_t offset_in_page = phys_addr & ~PAGE_MASK;
    ssize_t space_left_in_page = PAGE_SIZE - offset_in_page;
    // read until page boundary (or end of length)
    ssize_t chunk_to_read = 0;
    if (remaining < space_left_in_page) {
        chunk_to_read = remaining;
    } else{
        chunk_to_read = space_left_in_page;
    }

    pfn = PHYS_PFN(phys_addr);
    if (!pfn_valid(pfn)) {
        pr_debug("invalid PFN for phys=%pa\n", &phys_addr);
        return -EINVAL;
    }

    pr_debug("physical address: %pa\n", &phys_addr);
    // return accessible kernel address to caller
    *mapped_addr = __va(phys_addr);
    pr_debug("using virtual address %px for access\n", *mapped_addr);

    return chunk_to_read;
}

static int device_open(struct inode *inode, struct file *filp) {
    /*
    * We don't want to talk to two processes at the same time
    */
    if (atomic_cmpxchg(&device_is_open, 0, 1) != 0)
        return -EBUSY;
    try_module_get(THIS_MODULE);

    filp->f_mode |= FMODE_UNSIGNED_OFFSET;
    pr_debug("device opened\n");

    return SUCCESS;
}

static int device_release(struct inode *inode, struct file *file) {
    atomic_set(&device_is_open, 0);      /* We're now ready for our next caller */

    /*
     * Decrement the usage count, or else once you opened the file, you'll
     * never get get rid of the module.
     */
    module_put(THIS_MODULE);

    pr_debug("device released\n");

    return 0;
}

/**
 * read physical memory at address specified by offset.
 */
static ssize_t device_read(struct file *filp,   /* see include/linux/fs.h   */
        char __user *buffer,   /* buffer to fill with data */
        size_t length,  /* length of the buffer     */
        loff_t *offset  /* phys address to read from     */
        ) {
    ssize_t bytes_read = -EINVAL;
    ssize_t bytes_done = 0;
    phys_addr_t phys_addr;

    if (!buffer || !offset || length == 0) {
        pr_info("read invalid arguments\n");
        goto exit;
    }

    if (*offset < 0) {
        pr_info("read invalid negative offset=%lld\n", *offset);
        bytes_read = -EINVAL;
        goto exit;
    }

    pr_debug("read len=%zu from phys=%llx\n", length, *offset);

    // read from offset into buffer
    void *mapped_addr = NULL;
    phys_addr = (phys_addr_t)*offset;

    size_t copy_not_done = 0;
    ssize_t remaining = length;
    char __user *buffer_remaining = buffer;

    // check if we will cross page boundary and split into chunks as needed
    while (remaining > 0) {
        // check where we are within page and convert addr (VA or PA) to mapped_addr in kernel space
        ssize_t chunk_to_read = get_len_and_mapped_addr(&mapped_addr, phys_addr, remaining);
        ssize_t copied;
        // Check if address was valid and mapped_addr is usable
        if (chunk_to_read < 0) {
            bytes_read = (bytes_done > 0) ? bytes_done : chunk_to_read;
            goto exit;
        }

        pr_debug("reading chunk=%zd at phys=%pa\n", chunk_to_read, &phys_addr);

        // copy from kernel VA to buffer
        copy_not_done = copy_to_user(buffer_remaining, mapped_addr, chunk_to_read);
        copied = chunk_to_read - copy_not_done;
        bytes_done += copied;

        // check that all bytes were copied to buffer
        if (copy_not_done != 0) {
            pr_warn("copy_to_user partial: %zu bytes not copied\n", copy_not_done);
            bytes_read = (bytes_done > 0) ? bytes_done : -EFAULT;
            goto exit;
        }

        buffer_remaining += copied;
        phys_addr += copied;
        remaining -= copied;
    }

    bytes_read = bytes_done;
    *offset += bytes_read;
    pr_debug("finished reading %zd bytes; new offset=%llx\n", bytes_read, *offset);

exit:
    return bytes_read;

}

/*
 * Called when a process writes to dev file. Writes to physical mem device is pointed to
 * Returns the number of bytes written (including partial writes) and negative errorCode or 0 on failure.
 */
static ssize_t device_write(struct file *filp, const char __user *buff, size_t len, loff_t * offset) {
    ssize_t bytes_written = -EINVAL;
    ssize_t bytes_done = 0;
    phys_addr_t phys_addr;

    if (!buff || !offset || len == 0) {
        pr_info("write invalid arguments\n");
        goto exit;
    }

    if (*offset < 0) {
        pr_info("write invalid negative offset=%lld\n", *offset);
        bytes_written = -EINVAL;
        goto exit;
    }

    phys_addr = (phys_addr_t)*offset;
    pr_debug("write len=%zu to phys=%llx\n", len, *offset);

    void *mapped_addr = NULL;
    size_t copy_not_done = 0;
    ssize_t remaining = len;
    const char __user *buffer_remaining = buff;

    // check if we will cross page boundary and split into chunks as needed
    while (remaining > 0) {
        // check where we are within page and convert phys_addr to mapped_addr in kernel space
        ssize_t chunk_to_read = get_len_and_mapped_addr(&mapped_addr, phys_addr, remaining);
        ssize_t copied;
        if (chunk_to_read < 0) {
            bytes_written = (bytes_done > 0) ? bytes_done : chunk_to_read;
            goto exit;
        }

        // copy from buffer to kernel VA
        copy_not_done = copy_from_user(mapped_addr, buffer_remaining, chunk_to_read);
        copied = chunk_to_read - copy_not_done;
        bytes_done += copied;

        // check that all bytes were copied to memory
        if (copy_not_done != 0) {
            pr_warn("copy_from_user partial: %zu bytes not copied\n", copy_not_done);
            bytes_written = (bytes_done > 0) ? bytes_done : -EFAULT;
            goto exit;
        }

        buffer_remaining += copied;
        phys_addr += copied;
        remaining -= copied;
    }

    bytes_written = bytes_done;
    *offset += bytes_written;
    pr_debug("finished writing %zd bytes; new offset=%llx\n", bytes_written, *offset);

exit:
    return bytes_written;
}

loff_t device_llseek(struct file *filp, loff_t off, int whence) {
    loff_t newpos;

    pr_debug("llseek(filp:%px, off:%llx, whence:%d)\n", filp, off, whence);

    switch (whence) {
        case 0: /* SEEK_SET */
            newpos = off;
            break;

        case 1: /* SEEK_CUR */
            newpos = filp->f_pos + off;
            break;

        default: /* can't happen */
            return -EINVAL;
    }

    if (newpos < 0)
        return -EINVAL;

    pr_debug("newpos: %llx\n", newpos);

    filp->f_pos = newpos;
    return newpos;
}

/**
 * Command handler for bit flip IOCTL command
 *
 * User specifies physical address offset + bit offset within that byte, this command flips that bit
 * Fixes issue of read/edit/write being handled separatly in userspace
 *
 */
static long device_ioctl(struct file *file, unsigned int cmd, unsigned long arg) {
    struct bit_flip_request req;
    phys_addr_t phys;
    unsigned long pfn;
    void *vaddr;
    uint8_t *ptr;

    if (atomic_read(&device_is_open) == 0) {
        pr_warn("ioctl called while device is not open\n");
        return -ENODEV;
    }

    if (_IOC_TYPE(cmd) != FAULTMEM_IOC_MAGIC)
        return -ENOTTY;

    // Only 1 command type is supported
    if (cmd != FAULTMEM_BIT_FLIP)
        return -EINVAL;

    // Read PA and bit offset from userspace
    if (copy_from_user(&req, (void __user *)arg, sizeof(req)))
        return -EFAULT;

    if (req.bit > 7)
        return -EINVAL;

    phys = req.phys_addr;
    pfn = PHYS_PFN(phys);

    if (!pfn_valid(pfn))
        return -EINVAL;

    pr_debug("ioctl bit flip: phys=%pa bit=%u\n", &phys, req.bit);

    // Translate PA to VA using RAM's direct mapping in kernel's address space
    vaddr = __va((phys_addr_t)pfn << PAGE_SHIFT);
    // Find offset within page and add to VA
    ptr = (uint8_t *)vaddr + (phys & (PAGE_SIZE - 1));

    // flip exactly one bit
    *ptr ^= (1 << req.bit);

    pr_debug("ioctl bit flip complete\n");

    return 0;
}

/*
 * This structure will hold the functions to be called
 * when a process does something to the device we
 * created. Since a pointer to this structure is kept in
 * the devices table, it can't be local to
 * init_module. NULL is for unimplemented functions.
 *
 * NOTE: pread is supported if read, write, and llseek are
 */
static struct file_operations fops = {
    .owner = THIS_MODULE,
	.read = device_read,
	.write = device_write,
    .unlocked_ioctl = device_ioctl,
	// .ioctl = device_ioctl,
	.open = device_open,
	.release = device_release,	/* a.k.a. close */
    .llseek = device_llseek
};

static struct miscdevice faultmem_dev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = DEVICE_NAME,
    .mode = 0666,
    .fops = &fops
};

static int __init faultmem_init(void) {
    int ret;

    ret = misc_register(&faultmem_dev);
    if (ret) {
        pr_err("failed to register misc device: %d\n", ret);
        return ret;
    }

    pr_info("module loaded\n");
    return 0;
}

static void __exit faultmem_exit(void) {
    misc_deregister(&faultmem_dev);
    pr_info("module unloaded\n");
}

module_init(faultmem_init);
module_exit(faultmem_exit);

// TODO: permission checks or overrides (and replacing afterwards?)?
// __va(PA) only works for direct-mapped mem, so must check:
// if (!pfn_valid(PHYS_PFN(phys)))
    // return -EINVAL;
// in actively cached mem (??), use flush_dcache_page() so all cores can see