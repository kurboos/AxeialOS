#!/bin/bash

#./run.sh img > run with .img
#./run.sh iso > run with .iso

DISK_IMG="./.Build/axeialos.img"
ISO_IMG="./.Build/axeialos.iso"

if [ "$1" = "img" ]; then
    echo "running with disk image..."
    qemu-system-x86_64 \
      -bios /usr/share/ovmf/OVMF.fd \
      -machine q35 \
      -m 512M \
      -smp 4 \
      -serial file:debug.log \
      -d guest_errors,cpu_reset,int,pcall \
      -D qemu.log \
      -no-reboot \
      -no-shutdown \
      -device ahci,id=ahci0 \
      -drive id=hd0,file="$DISK_IMG",format=raw,if=none \
      -device ide-hd,drive=hd0,bus=ahci0.0

elif [ "$1" = "iso" ]; then
    echo "running with ISO image..."
    qemu-system-x86_64 \
      -bios /usr/share/ovmf/OVMF.fd \
      -machine q35 \
      -m 512M \
      -smp 4 \
      -serial file:debug.log \
      -d guest_errors,cpu_reset,int,pcall \
      -D qemu.log \
      -no-reboot \
      -no-shutdown \
      -device ahci,id=ahci0 \
      -drive id=hd0,file="$ISO_IMG",format=raw,if=none \
      -device ide-hd,drive=hd0,bus=ahci0.0

else
    echo "args expects > 'img' or 'iso'"
    exit 1
fi