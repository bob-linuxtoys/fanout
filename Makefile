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
	make -C $(KERNELDIR) M=$(PWD) modules_install
endif
