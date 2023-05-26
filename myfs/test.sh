#!/bin/bash

insmod myfs.ko && mkdir -p /mnt/myfs && mount -t myfs none /mnt/myfs && ls -laid /mnt/myfs
