BuildDirectory 		:= .Build
EFIRoot 			:= $(BuildDirectory)/EFI

AlternateDiskImage 	:= $(BuildDirectory)/axeialos.img
VboxVDIBootable 	:= $(BuildDirectory)/axeialos.vdi

Limine 				:= limine
AxeKrnl 			:= Kernel/.Build/bin/axekrnl

#	Stupid Vbox registry problems
TestUUID			:= 8cb1c632-6c16-4d0c-b105-9152e603faa6

.PHONY: \
BuildAxe \
clean \
AxeKrnl \
VboxVDIImage

BuildAxe: \
AxeKrnl \
VboxVDIImage

AxeKrnl:

	@mkdir -p $(BuildDirectory) $(EFIRoot)
	
	@$(MAKE) -C Kernel
	@$(MAKE) -C SysApps
	@$(MAKE) -C BootImg

	@cp $(AxeKrnl) $(EFIRoot)/axekrnl

VboxVDIImage: AxeKrnl

#	Because Registry issues
	@VBoxManage closemedium disk "$(VboxVDIBootable)" --delete 2>/dev/null || true
	@VBoxManage closemedium disk "{$(TestUUID)}" --delete 2>/dev/null || true

	@rm -f "$(VboxVDIBootable)"
	@mkdir -p $(EFIRoot)/EFI/BOOT

#	Limine
	@cp $(Limine)/BOOTX64.EFI $(EFIRoot)/EFI/BOOT/
	@cp Boot/limine.cfg $(EFIRoot)/

	@dd if=/dev/zero of=$(AlternateDiskImage) bs=1M count=64
	@parted $(AlternateDiskImage) mklabel gpt
	@parted $(AlternateDiskImage) mkpart primary fat32 1MiB 100%
	@parted $(AlternateDiskImage) set 1 esp on
	@sudo losetup -P /dev/loop0 $(AlternateDiskImage)
	@sudo mkfs.fat -F32 /dev/loop0p1
	@sudo mount /dev/loop0p1 /mnt
	@sudo cp -r $(EFIRoot)/* /mnt/
	@sudo umount /mnt
	@sudo losetup -d /dev/loop0
	@VBoxManage convertfromraw $(AlternateDiskImage) $(VboxVDIBootable) --format VDI --uuid
	{
	    $(TestUUID)
	}
#	@rm $(AlternateDiskImage)

clean:

#	Vbox clean up because registry
	@VBoxManage closemedium disk "$(VboxVDIBootable)" --delete 2>/dev/null || true
	@VBoxManage closemedium disk "{$(TestUUID)}" --delete 2>/dev/null || true

	@rm -rf $(BuildDirectory)
	@$(MAKE) -C Kernel clean
	@$(MAKE) -C BootImg clean
	@$(MAKE) -C SysApps clean
