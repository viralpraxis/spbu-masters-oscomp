install: tmpfs.c
	gcc tmpfs.c -Wall -D_FILE_OFFSET_BITS=64 -lfuse -o tmpfs

mount: tmpfs
	./tmpfs mountpoint

mount_debug:
	./tmpfs -d mountpoint

unmount:
	umount mountpoint

.PHONY: mount mount_debug unmount

default: install