KERNEL_SRC_DIR = /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

obj-m += faultmem.o

all: faultmem.ko

faultmem.ko:
	make -C $(KERNEL_SRC_DIR) M=$(PWD) modules
	/usr/lib/linux-kbuild-$(shell uname -r | awk -F'.' '{print $$1"."$$2}')/scripts/sign-file sha256 ~/mok/MOK.priv ~/mok/MOK.der ./faultmem.ko

clean:
	make -C $(KERNEL_SRC_DIR) M=$(PWD) clean
