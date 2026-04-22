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
	/usr/lib/linux-kbuild-$(shell uname -r | awk -F'.' '{print $$1"."$$2}')/scripts/sign-file sha256 ~/mok/MOK.priv ~/mok/MOK.der $(MODULE_DIR)/faultmem.ko

clean:
	$(MAKE) -C $(KERNEL_SRC_DIR) M=$(MODULE_DIR) clean
endif
