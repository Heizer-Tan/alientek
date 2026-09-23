# 出树 IIO 模块：依赖内核 CONFIG_IIO 提供的 iio_* 符号
obj-m := icm20608.o

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
KERNELDIR ?= $(KERNEL_SRC)
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KERNEL_SRC) O=$(KERNELDIR) M=$(PWD) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC) O=$(KERNELDIR) M=$(PWD) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) O=$(KERNELDIR) M=$(PWD) clean
