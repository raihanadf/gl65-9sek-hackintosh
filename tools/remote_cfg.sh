#!/bin/sh
# pull or push the opencore config on the laptop's ssd efi partition (disk0s4)
# usage: GL65=user@laptop-ip tools/remote_cfg.sh pull|push <local config.plist>
# push keeps a config.plist.bak-HHMMSS next to the old file and prints both hashes
set -e
H=${GL65:?set GL65=user@laptop-ip}
F=${2:?local config.plist path}
case "$1" in
  pull) ssh -o BatchMode=yes "$H" 'sudo diskutil mount disk0s4 >/dev/null; sudo cat /Volumes/BOOT/EFI/OC/config.plist' > "$F" ;;
  push) ssh -o BatchMode=yes "$H" 'sudo diskutil mount disk0s4 >/dev/null; sudo cp /Volumes/BOOT/EFI/OC/config.plist /Volumes/BOOT/EFI/OC/config.plist.bak-$(date +%H%M%S); sudo tee /Volumes/BOOT/EFI/OC/config.plist >/dev/null; sudo cat /Volumes/BOOT/EFI/OC/config.plist | shasum | cut -c1-12' < "$F"
        shasum "$F" | cut -c1-12 ;;
  *) echo "usage: GL65=user@host $0 pull|push <config.plist>"; exit 1 ;;
esac
