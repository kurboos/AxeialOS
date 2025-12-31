#color
RED    := \033[0;31m
YELLOW := \033[0;33m
GREEN  := \033[0;32m
RESET  := \033[0m

BuildDirectory := .Build
EFIRoot        := $(BuildDirectory)/EFI
DiskImg        := $(BuildDirectory)/axeialos.img

Limine := limine
AxeKrnl := Kernel/.Build/bin/axekrnl

.PHONY: \
BuildAxe \
clean \
AxeKrnl \
FinalImg \
RebuildLibc

BuildAxe: AxeKrnl FinalImg
	@echo "$(GREEN)[SUCCESS] build completed successfully$(RESET)"

AxeKrnl:
	@mkdir -p $(BuildDirectory) $(EFIRoot)
	@echo "$(YELLOW)[INFO] Building Kernel, SysApps, Firmware, BootImg...$(RESET)"
	@$(MAKE) -C Kernel || (echo "$(RED)[ERROR] Kernel build failed$(RESET)" && exit 1)
	@$(MAKE) -C SysApps || (echo "$(RED)[ERROR] SysApps build failed$(RESET)" && exit 1)
	@$(MAKE) -C Firmware || (echo "$(RED)[ERROR] Firmware build failed$(RESET)" && exit 1)
	@$(MAKE) -C BootImg || (echo "$(RED)[ERROR] BootImg build failed$(RESET)" && exit 1)
	@cp $(AxeKrnl) $(EFIRoot)/axekrnl
	@echo "$(GREEN)[SUCCESS] kernel copied to EFI root$(RESET)"

FinalImg: AxeKrnl
	@echo "$(YELLOW)[INFO] creating test disk image...$(RESET)"
	@rm -f "$(DiskImg)"
	@mkdir -p $(EFIRoot)/EFI/BOOT
	@cp $(Limine)/BOOTX64.EFI $(EFIRoot)/EFI/BOOT/
	@cp Boot/limine.cfg $(EFIRoot)/
	@dd if=/dev/zero of=$(DiskImg) bs=1M count=64
	@parted $(DiskImg) mklabel gpt
	@parted $(DiskImg) mkpart primary fat32 1MiB 100%
	@parted $(DiskImg) set 1 esp on
	@sudo losetup -P /dev/loop0 $(DiskImg)
	@sudo mkfs.fat -F32 /dev/loop0p1
	@sudo mount /dev/loop0p1 /mnt
	@sudo cp -r $(EFIRoot)/* /mnt/
	@sudo umount /mnt
	@sudo losetup -d /dev/loop0
	@echo "$(GREEN)[SUCCESS] disk image created at: $(DiskImg)$(RESET)"

#just incase i edit something
RebuildLibc:
	@$(MAKE) -C CLibrary clean
	@$(MAKE) -C CLibrary all
	@$(MAKE) -C CLibrary install

clean:
	@echo "$(YELLOW)[INFO] cleaning build...$(RESET)"
	@rm -rf $(BuildDirectory)
	@$(MAKE) -C Kernel clean
	@$(MAKE) -C BootImg clean
	@$(MAKE) -C SysApps clean
	@$(MAKE) -C Firmware clean
	@echo "$(GREEN)[SUCCESS] clean complete$(RESET)"