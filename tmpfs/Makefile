build: tmpfs.c structs.h utils.h
	gcc tmpfs.c utils.c -Wall -D_FILE_OFFSET_BITS=64 -lfuse -o tmpfs

mount: tmpfs
	./tmpfs mountpoint

mount_debug:
	./tmpfs -d mountpoint

unmount:
	umount mountpoint

test: test.sh
	./test.sh

.PHONY: mount_debug unmount

default: build