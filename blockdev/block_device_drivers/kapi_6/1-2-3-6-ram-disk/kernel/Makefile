EXTRA_CFLAGS = -Wall -g -Wno-unused

obj-m = ram-disk.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

