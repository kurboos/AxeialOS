qemu-system-x86_64 \
    -bios /usr/share/ovmf/OVMF.fd \
    -drive format=raw,file=./.Build/axeialos.img \
    -m 255M \
    -smp 4 \
    -serial file:debug.log
