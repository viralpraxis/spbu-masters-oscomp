set -e

cd /mnt/myfs

dd if=/dev/urandom of=rnd bs=1M count=20

dd if=rnd bs=100K 2>/dev/null | sha256sum
dd if=rnd bs=1M 2>/dev/null | sha256sum
dd if=rnd bs=10M 2>/dev/null | sha256sum
dd if=rnd bs=20M 2>/dev/null | sha256sum
dd if=rnd bs=30M 2>/dev/null | sha256sum
