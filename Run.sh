qemu-system-x86_64 \
    -bios /usr/share/ovmf/OVMF.fd \
    -drive format=raw,file=./.Build/axeialos.img \
    -m 512M \
    -smp 4 \
    -serial file:debug.log \
    -d guest_errors,cpu_reset,int,pcall \
    -D qemu.log \
    -no-reboot \
    -no-shutdown