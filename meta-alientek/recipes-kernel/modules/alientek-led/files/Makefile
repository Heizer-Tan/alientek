# 出树 LED misc 模块：/dev/alientek-led
obj-m := alientek-led.o

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
KERNELDIR ?= $(KERNEL_SRC)
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KERNEL_SRC) O=$(KERNELDIR) M=$(PWD) modules

modules_install:
	$(MAKE) -C $(KERNEL_SRC) O=$(KERNELDIR) M=$(PWD) modules_install

clean:
	$(MAKE) -C $(KERNEL_SRC) O=$(KERNELDIR) M=$(PWD) clean
