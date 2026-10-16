#!/bin/zsh
# trim stress: 10 rounds of a 2 gb write, delete, unmount and mount on a scratch apfs volume
# usage: sudo trimstress.sh   (creates and removes the TrimTest volume in disk1)
set -e
diskutil apfs addVolume disk1 APFS TrimTest >/dev/null
vol=/Volumes/TrimTest
for i in {1..10}; do
  start=$(date +%s)
  dd if=/dev/urandom of=$vol/fill bs=1m count=2048 2>/dev/null
  sync
  rm $vol/fill
  sync
  diskutil unmount $vol >/dev/null
  diskutil mount TrimTest >/dev/null
  echo "round $i ok in $(( $(date +%s) - start ))s"
done
diskutil apfs deleteVolume TrimTest >/dev/null
echo "all rounds ok"
