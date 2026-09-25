obj-m := ap3216c.o

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
KERNELDIR ?= $(KERNEL_SRC)
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KERNEL_SRC) O=$(KERNELDIR) M=$(PWD) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC) O=$(KERNELDIR) M=$(PWD) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) O=$(KERNELDIR) M=$(PWD) clean
