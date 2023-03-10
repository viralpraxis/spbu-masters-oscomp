#!/bin/bash

set -e

mkdir mountpoint/test-dir

for i in {1..25}
do
  mkdir "mountpoint/dir-${i}"
done

for i in {1..25}
do
  touch "mountpoint/test-dir/dir-${i}"
done

for i in {1..25}
do
  echo $i > "mountpoint/test-dir/dir-${i}" 
done


for i in {1..25}
do
  stat "mountpoint/test-dir/dir-${i}" > /dev/null
done



for i in {1..25}
do
  if [[ $(< "mountpoint/test-dir/dir-${i}" ) != "${i}" ]]; then
      echo "Error! File mountpoint/test-dir/dir-${i} is invalid" 1>&2
      exit 1
  fi
done

echo OK