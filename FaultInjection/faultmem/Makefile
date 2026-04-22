ifneq ($(KERNELRELEASE),)
obj-m += faultmem.o
faultmem-objs := src/faultmem.o
ccflags-y += -I$(src)/include
else
KERNEL_SRC_DIR ?= /lib/modules/$(shell uname -r)/build
MODULE_DIR := $(CURDIR)

all: faultmem.ko

faultmem.ko:
	$(MAKE) -C $(KERNEL_SRC_DIR) M=$(MODULE_DIR) modules

clean:
	$(MAKE) -C $(KERNEL_SRC_DIR) M=$(MODULE_DIR) clean
endif
