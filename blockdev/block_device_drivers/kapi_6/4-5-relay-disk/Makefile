KERNELRELEASE ?= $(shell uname -r)
KERNELDIR ?= /lib/modules/$(KERNELRELEASE)/build
PWD := $(shell pwd)

obj-m += relay-disk.o

all:
		make -C /lib/modules/$(KERNELRELEASE)/build M=$(PWD) modules

install:
		make -C /lib/modules/$(KERNELRELEASE)/build M=$(PWD) modules_install

clean:
		make -C /lib/modules/$(KERNELRELEASE)/build M=$(PWD) clean

insmod:
		sudo insmod membuf.ko

rmmod:
		sudo rmmod membuf

