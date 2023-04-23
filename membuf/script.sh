#!/bin/bash

set -e

echo 123 > /dev/membuf0
echo 456 > /dev/membuf1

cat /dev/membuf0
cat /dev/membuf1

echo 15 > /sys/module/membuf/parameters/devices_count
cat /sys/module/membuf/parameters/devices_count

echo 16 > /sys/module/membuf/parameters/devices_count
cat /sys/module/membuf/parameters/devices_count
