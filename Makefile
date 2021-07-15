ifneq ($(KERNELRELEASE),)
	obj-m += fanout.o
else
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)
 
all:
	make -C $(KERNELDIR) M=$(PWD) modules
 
clean:
	make -C $(KERNELDIR) M=$(PWD) clean
 
install:
	mkdir -p /lib/modules/$(shell uname -r)/kernel/drivers/char
	cp fanout.ko /lib/modules/$(shell uname -r)/kernel/drivers/char
	depmod -a
endif

udev:
	cp udev.d/fanout.rules /etc/udev/rules.d/90-fanout.rules
	udevadm control --reload-rules
