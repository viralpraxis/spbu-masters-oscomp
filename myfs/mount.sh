#!/bin/bash

set -ex

insmod myfs.ko && mkdir -p /mnt/myfs && mount -t myfs none /mnt/myfs && ls -laid /mnt/myfs

# echo 123 | tee /mnt/myfs/test.txt
# ls -laid /mnt/myfs/test.txt

# mkdir /mnt/myfs/dir

# echo 123 | sudo tee /mnt/myfs/dir/test-1.txt
# echo 456 | sudo tee /mnt/myfs/dir/test-2.txt

# cat /mnt/myfs/dir/test-1.txt
# cat /mnt/myfs/dir/test-2.txt

# dd if=/dev/zero of=/mnt/myfs/dir/test-1.txt bs=1024 count=1
# dd if=/dev/zero of=/mnt/myfs/dir/test-2.txt bs=1024 count=4

# sudo cat /mnt/myfs/dir/test-1.txt
# sudo cat /mnt/myfs/dir/test-2.txt
