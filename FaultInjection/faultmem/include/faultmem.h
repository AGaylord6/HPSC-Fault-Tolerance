/*
*  faultmem.h
*
*  The IOCTL command declarations here have to be in a header file, because
*  they need to be known both to the kernel module
*  (in faultmem.c) and the process calling ioctl
*/

#ifndef CHARDEV_H
#define CHARDEV_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define SUCCESS 0

// name in /proc/devices
#define DEVICE_NAME "faultmem"

#define FAULTMEM_IOC_MAGIC 'f'

struct bit_flip_request {
    __u64 phys_addr;
    __u8 bit;
};

#define FAULTMEM_BIT_FLIP _IOW(FAULTMEM_IOC_MAGIC, 0, struct bit_flip_request)
/*
 * _IOW means it's an ioctl command that reads from userspace and writes to kernel module
 *
 * First arg is identifying ("magic") letter/number. Some devices use their major number, we hardcode them
 *
 * The second argument is the sequence number of the command (which we only have 1 of)
 *
 * The third argument is the type being passed to kernel
 */

#endif